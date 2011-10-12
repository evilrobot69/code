// Scrabble move generator. Given a word list, board, and your current tiles,
// outputs all legal moves ranked by point value.

package scrabblish

import ("appengine"; "appengine/memcache"; "appengine/urlfetch"; "bytes";
        "encoding/binary"; "fmt"; "gob"; "http";
        "scrabblish/scrabble"; "scrabblish/trie"; "scrabblish/util")

func init() {
  http.HandleFunc("/solve", solve)
}

func bToI(b []byte) int {
  buf := bytes.NewBuffer(b)
  var i int
  binary.Read(buf, binary.LittleEndian, &i)
  return i
}

func iToB(i int) []byte {
  b := make([]byte, 4)
  for j := 0; j < 4; j++ {
    b[i] = byte(i >> (8 * j))
  }
  return b
}

func getKeys(c appengine.Context, key string) (keys []string) {
  item, err := memcache.Get(c, key);
  if err != nil {
    c.Infof("Could not retrieve number of keys with error: %s", err.String())
    return
  }
  num := bToI(item.Value)
  keys = make([]string, num)
  for i := 0; i < num; i++ {
    keys[i] = fmt.Sprintf("%s%d", key, i)
  }
  return
}

const MAX_MEMCACHE_VALUE_SIZE = 1000000

func splitMemcache(key string, data []byte) (items []*memcache.Item) {
  item := new(memcache.Item)
  item.Key = key
  item.Value = iToB((len(data) - 1) / MAX_MEMCACHE_VALUE_SIZE + 1)
  items = append(items, item)
  for i := 0; i < len(data); i += MAX_MEMCACHE_VALUE_SIZE {
    item = new(memcache.Item)
    item.Key = fmt.Sprintf("%s%d", key, i / MAX_MEMCACHE_VALUE_SIZE)
    j := i + MAX_MEMCACHE_VALUE_SIZE
    if j > len(data) { j = len(data) }
    item.Value = data[i:j]
    items = append(items, item)
  }
  return
}

func joinMemcache(items map[string]*memcache.Item) (data []byte) {
  for _, value := range(items) {
    data = append(data, value.Value...)
  }
  return
}

func solve(w http.ResponseWriter, r *http.Request) {
  c := appengine.NewContext(r)
  var dict *trie.Trie
  // Get our dictionary.
  items, err := memcache.GetMulti(c, getKeys(c, "dict"))
  if err != nil || len(items) == 0 {
    client := urlfetch.Client(c)
    resp, err := client.Get("http://scrabblish.appspot.com/twl")
    if err != nil {
      c.Errorf("Could not retrieve twl with error: %s", err.String())
      http.Error(w, err.String(), http.StatusInternalServerError)
      return
    }
    defer resp.Body.Close()
    dict = util.ReadWordList(resp.Body)
    var data bytes.Buffer
    enc := gob.NewEncoder(&data)
    err = enc.Encode(dict)
    if err != nil {
      c.Errorf("Could not encode twl with error: %s", err.String())
    }
    errs := memcache.SetMulti(c, splitMemcache("dict", data.Bytes()))
    for i := 0; i < len(errs); i++ {
      if errs[i] != nil {
        c.Errorf("Could not cache dict chunk %d with error: %s", i,
                 errs[i].String())
      }
    }
  } else {
    data := bytes.NewBuffer(joinMemcache(items))
    dec := gob.NewDecoder(data)
    dec.Decode(dict)
  }

  // Get params from request.
  err = r.ParseForm()
  if err != nil {
    c.Errorf("Could not parse form with error: %s", err.String())
    http.Error(w, err.String(), http.StatusInternalServerError)
    return
  }
  board := util.ReadBoard(r.FormValue("board"))
  tiles := util.ReadTiles(r.FormValue("tiles"))
  letterValues := util.ReadLetterValues(
      "1 4 4 2 1 4 3 4 1 10 5 1 3 1 1 4 10 1 1 1 2 4 4 8 4 10")

  moveList := scrabble.GetMoveList(dict, board, tiles,
                                   letterValues)

  fmt.Fprint(w, util.PrintMoveList(moveList, 25))
}

