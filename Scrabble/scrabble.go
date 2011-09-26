// Scrabble move generator. Given a word list, board, and your current tiles,
// outputs all legal moves ranked by point value.

package scrabble

import ("container/vector"; "flag"; "fmt"; "os";
        "cross_check"; "moves"; "sort_with"; "trie"; "util")

var wordListFlag = flag.String(
    "w", "twl.txt",
    "File with space-separated list of legal words, in upper-case.")
var boardFlag = flag.String(
    "b", "",
    "File with board structure. Format: * indicates starting point, 1 and 2 " +
    "indicate double and triple word score tiles, 3 and 4 indicate double " +
    "and triple letter score tiles, - indicates blank tiles, and upper-case " +
    "letters indicate existing words.")
var tilesFlag = flag.String(
    "t", "", "List of all 7 player tiles, in upper-case.")
var letterValuesFlag = flag.String(
    "l", "1 4 4 2 1 4 3 4 1 10 5 1 3 1 1 4 10 1 1 1 2 4 4 8 4 10",
    "Space-separated list of letter point values, from A-Z.")
var numResultsFlag = flag.Int(
    "n", 25, "Maximum number of results to output.")

func BlankScore(score int, letterValue int, tile byte) int {
  wordMultiplier, letterMultiplier := util.TileMultipliers(tile)
  return score / wordMultiplier - letterMultiplier * letterValue
}


// Retrieve whether or not we have tiles that can possibly follow prefix in
// dict.
func CanFollow(dict *trie.Trie, prefix string, tiles map[byte] int) bool {
  following := dict.Following(prefix)
  count, existing := tiles[byte(' ')]
  if len(following) > 0 && existing && count > 0 { return true }
  for i := 0; i < len(following); i++ {
    count, existing = tiles[following[i]]
    if existing && count > 0 { return true }
  }
  return false
}

func MoveRight(
    dict *trie.Trie, board [][]byte, tiles map[byte] int,
    letterValues map[byte] int, crossChecks map[int] map[byte] int,
    possibleMove moves.Move) (moveList *vector.Vector) {
  moveList = new(vector.Vector)
  prefixEnd := moves.Location{possibleMove.Start.X,
                              possibleMove.Start.Y + len(possibleMove.Word)}
  for ; prefixEnd.Y < util.BOARD_SIZE && !util.Available(board, &prefixEnd);
      prefixEnd.Y++ {
    possibleMove.Word += string(board[prefixEnd.X][prefixEnd.Y])
  }
  if CanFollow(dict, possibleMove.Word, tiles) {
    moveList.AppendVector(
      Extend(dict, board, tiles, letterValues, crossChecks, possibleMove,
             false))
  }
  return
}

func Extend(
    dict *trie.Trie, board [][]byte, tiles map[byte] int,
    letterValues map[byte] int, crossChecks map[int] map[byte] int,
    possibleMove moves.Move, left bool) (moveList *vector.Vector) {
  moveList = new(vector.Vector)
  var positionCrossChecks map[byte] int
  var existing bool
  if left {
    if !util.Available(board, &possibleMove.Start) { return }
    positionCrossChecks, existing = crossChecks[possibleMove.Start.Hash()]
  } else {
    endLocation := moves.Location{possibleMove.Start.X,
                                  possibleMove.Start.Y + len(possibleMove.Word)}
    if !util.Available(board, &endLocation) { return }
    positionCrossChecks, existing = crossChecks[endLocation.Hash()]
  }
  for tile, count := range(tiles) {
    if count == 0 { continue }
    verticallyScoredLetters := make(map[byte] int)
    if tile != byte(' ') {
      if existing {
        score, tileExisting := positionCrossChecks[tile]
        if tileExisting { verticallyScoredLetters[tile] = score }
      } else {
        verticallyScoredLetters[tile] = 0
      }
    } else if existing {
      for i, score := range(positionCrossChecks) {
        var placedCol int
        if left {
          placedCol = possibleMove.Start.Y
        } else {
          placedCol = possibleMove.Start.Y + len(possibleMove.Word)
        }
        verticallyScoredLetters[i - 26] = BlankScore(
            score, letterValues[i], board[possibleMove.Start.X][placedCol])
      }
    } else {
      for i := byte('A'); i <= byte('Z'); i++ {
        verticallyScoredLetters[i - 26] = 0
      }
    }
    for letter, score := range(verticallyScoredLetters) {
      placedMove := possibleMove.Copy()
      if left {
        placedMove.Word = string(letter) + placedMove.Word
      } else {
        placedMove.Word += string(letter)
      }
      placedMove.Score += score
      if dict.Find(placedMove.Word) {
        score = placedMove.Score
        util.Score(board, letterValues, &placedMove)
        moveList.Push(placedMove)
        placedMove.Score = score
      }
      tiles[letter]--
      moveList.AppendVector(
        MoveRight(dict, board, tiles, letterValues, crossChecks, placedMove))
      if left {
        placedMove.Start.Y--
        moveList.AppendVector(
          Extend(dict, board, tiles, letterValues, crossChecks, placedMove,
                 true))
      }
      tiles[letter]++
    }
  }
  return
}

func GetMoveList(
    dict *trie.Trie, board [][]byte, tiles map[byte] int,
    letterValues map[byte] int,
    crossChecks map[int] map[byte] int) (moveList *vector.Vector) {
  moveList = new(vector.Vector)
  for i := 0; i < util.BOARD_SIZE; i++ {
    for j := 0; j < util.BOARD_SIZE; j++ {
      possibleMove := moves.Move{
        Word: "", Score: 0, Start: moves.Location{i, j},
        Direction: moves.ACROSS }
      if board[i][j] == '*' {
        moveList.AppendVector(Extend(
            dict, board, tiles, letterValues, crossChecks, possibleMove,
            true))
      } else if board[i][j] >= 'A' && board[i][j] <= 'Z' {
        possibleMove.Start.Y--
        possibleMove.Word = string(board[i][j])
        moveList.AppendVector(Extend(
            dict, board, tiles, letterValues, crossChecks,
            possibleMove, true))
        possibleMove.Start.Y++
        moveList.AppendVector(MoveRight(
            dict, board, tiles, letterValues, crossChecks, possibleMove))
        possibleMove.Word = ""
        possibleMove.Start.X--
        moveList.AppendVector(Extend(
            dict, board, tiles, letterValues, crossChecks, possibleMove,
            true))
        possibleMove.Start.X += 2
        moveList.AppendVector(Extend(
            dict, board, tiles, letterValues, crossChecks, possibleMove,
            true))
      }
    }
  }
  return
}

func SetDirection(direction moves.Direction, moveList *vector.Vector) {
  for i := 0; i < moveList.Len(); i++ {
    move := moveList.At(i).(moves.Move)
    if (move.Direction != direction) {
      move.Start.X, move.Start.Y = move.Start.Y, move.Start.X
    }
    move.Direction = direction
    moveList.Set(i, move)
  }
}

func main() {
  flag.Parse()
  wordListFile, err := os.Open(*wordListFlag);
  defer wordListFile.Close();
  if err != nil {
    fmt.Printf("need valid file for -w, found %s\n", *wordListFlag)
    os.Exit(1)
  }
  boardFile, err := os.Open(*boardFlag);
  defer boardFile.Close();
  if err != nil {
    fmt.Printf("need valid file for -b, found %s\n", *boardFlag)
    os.Exit(1)
  }
  if len(*tilesFlag) != 7 {
    fmt.Printf("need 7 tiles in -t, found %d\n", len(*tilesFlag))
    os.Exit(1)
  }
  dict := util.ReadWordList(wordListFile)
  board := util.ReadBoard(boardFile)
  tiles := util.ReadTiles(*tilesFlag)
  letterValues := util.ReadLetterValues(*letterValuesFlag)
  transposedBoard := util.Transpose(board)

  // Get moves going both right and down.
  crossChecks := cross_check.GetCrossChecks(dict, transposedBoard, tiles,
                                            letterValues)
  moveList := GetMoveList(dict, board, tiles, letterValues, crossChecks)
  SetDirection(moves.ACROSS, moveList)
  downCrossChecks := cross_check.GetCrossChecks(dict, board, tiles,
                                                letterValues)
  downMoveList := GetMoveList(dict, transposedBoard, tiles, letterValues,
                              downCrossChecks)
  SetDirection(moves.DOWN, downMoveList)
  moveList.AppendVector(downMoveList)
  sort_with.SortWith(*moveList, moves.Greater)
  util.RemoveDuplicates(moveList)
  for i := 0;
      (*numResultsFlag <= 0 || i < *numResultsFlag) && i < moveList.Len(); i++ {
    fmt.Printf("%d. ", i + 1)
    move := moveList.At(i).(moves.Move)
    moves.PrintMove(&move)
    util.PrintMoveOnBoard(board, &move)
  }
}

