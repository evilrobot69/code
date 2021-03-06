# TODO(ariw): Some sort of registration interface so others can add their own
# games?
# TODO(ariw): Support for numeric, non-"username", and non-"account_token"
# requests is for legacy pre-Pebble 2.0 clients. Remove once most people have
# upgraded!

import base64
import hashlib
import hmac
import json
import logging
import os
import webapp2 as webapp

from google.appengine.api import memcache
from google.appengine.ext import db
from google.appengine.ext.webapp.util import run_wsgi_app

class Game(db.Model):
  name = db.StringProperty(required = True)
  mac_key = db.BlobProperty(required = True)
  # TODO(ariw): This should probably be auto-updated.
  low_score = db.IntegerProperty(default = 0)

class User(db.Model):
  name = db.StringProperty(required = True)
  account_token = db.StringProperty()
  ip_address = db.StringProperty(required = True)
  created = db.DateTimeProperty(auto_now_add = True)
  # Should probably be named num_low_scoring_games.
  num_zero_games = db.IntegerProperty(default = 0)

class HighScore(db.Model):
  # TODO(ariw): Should game and user be reference properties?
  game = db.StringProperty(required = True)
  user = db.StringProperty(required = True)
  score = db.IntegerProperty(required = True)
  created = db.DateTimeProperty(required = True, auto_now_add = True)

class NonceHandler(webapp.RequestHandler):
  def post(self):
    nonce = base64.standard_b64encode(os.urandom(16))
    # Use memcache to transiently store the nonce until the client sends us
    # their score.
    client = memcache.Client()
    client.set(nonce, True)
    self.response.out.write(json.dumps({"1": nonce, "nonce": nonce}))

def getEntitiesCacheKey(model, property, filter):
  return "%s,%s:%s" % (model, property, filter)

# Get a list of entities of type model with property=filter from either memcache
# or the datastore, updating memcache if we have to go to the datastore.
def getEntities(model, property, filter):
  cache_key = getEntitiesCacheKey(model, property, filter)
  client = memcache.Client()
  entities = client.get(cache_key)
  if entities is not None:
    return entities
  query = eval(model).all()
  query.filter("%s =" % property, filter)
  entities = [entity for entity in query]
  if not entities:
    return entities
  client.add(cache_key, entities)
  return entities

def getUser(username):
  users = getEntities("User", "name", username)
  if not users:
    return
  assert len(users) <= 1, "More than one user with name %s." % username
  return users[0]

def getGame(game):
  games = getEntities("Game", "name", game)
  if not games:
    return
  assert len(games) <= 1, "More than one game with name %s." % game
  return games[0]

def validateNonce(nonce):
  client = memcache.Client()
  validated_nonce = client.gets(nonce)
  # We compare-and-set the nonce to False here to prevent it from being reused
  # by other score submissions.
  return validated_nonce and client.cas(nonce, False, 1)

def getMac(game, score, nonce, mac_key):
  message = "%s%d%s" % (game, score, nonce)
  return hmac.new(mac_key, message, hashlib.sha256).hexdigest()

def getTwice(dictionary, property1, property2):
  return dictionary.get(property1, dictionary.get(property2, None))

class SubmitHandler(webapp.RequestHandler):
  # Get the user, get the game, verify the nonce, verify the hash, store the
  # score :).
  def post(self):
    request = json.loads(self.request.body)
    username = request.get(
        "username", self.request.headers.get("X-PEBBLE-ID", None))
    # Has the user configured their game yet?
    if username is None or username == "":
      logging.info("No username in request.")
      self.error(400)
      return

    game = getGame(getTwice(request, "name", "1"))
    if game is None:
      logging.error("Game %s not found, request: %s." % (
          getTwice(request, "name", "1"), self.request.body))
      self.error(400)
      return
    nonce = getTwice(request, "nonce", "4")
    if not validateNonce(nonce):
      logging.error("Nonce %s not found, request: %s." % (
          nonce, self.request.body))
      self.error(401)
      return
    score = getTwice(request, "score", "2")
    mac = getMac(str(game.name), score, nonce, game.mac_key)
    if mac != getTwice(request, "mac", "3"):
      logging.error(
          "Server MAC %s did not equal request MAC %s, request: %s." % (
              mac, getTwice(request, "mac", "3"), self.request.body))
      self.error(401)
      return

    account_token = request.get("account_token", None)
    # TODO(ariw): This view of User is not guaranteed to be consistent at all.
    user = getUser(username)
    if user is None:
      user = User(name = username, ip_address=self.request.remote_addr)
      if account_token is not None:
        user.account_token = account_token
      memcache.delete(getEntitiesCacheKey("User", "name", username))
      user.put()
    # TODO(ariw): Remove this overwriting of account_token once it's consistent
    # in Pebble and users have a chance to register their username.
    elif (user.account_token is not None and
          account_token != user.account_token):
      user.account_token = account_token
      memcache.delete(getEntitiesCacheKey("User", "name", username))
      user.put()
    # TODO(ariw): Re-enable account_token check once it's consistent in Pebble
    # and we have a way to indicate to users that their username is taken.
    # elif (user.account_token is not None and
    #       account_token != user.account_token):
    #   logging.info(
    #       "Server account token %s for user %s did not match request token " \
    #       "%s, request: %s." % (
    #           user.account_token, username, account_token, self.request.body))
    #   self.error(401)
    #   return
    # Don't store a highscore entry if the score was too low.
    if score <= game.low_score:
      user.num_zero_games += 1
      # We have to invalidate the user cache if we change the underlying user.
      memcache.delete(getEntitiesCacheKey("User", "name", username))
      user.put()
      return

    # Save the high score!
    highscore = HighScore(
        game = game.name, user = user.name, score = score)
    db.put(highscore)

_HIGHSCORE_HTML_TEMPLATE = """
  <li><b>%(highscore)s</b> - %(username)s"""

def highscoreHtml(highscore):
  return _HIGHSCORE_HTML_TEMPLATE % {
      "highscore": highscore.score, "username": highscore.user}

_HTML_TEMPLATE = """
  <!DOCTYPE HTML>
  <html lang="en">
  <head>
  <title>%(game)s Scores</title>
  </head>
  <body>
  <ol>
  %(list)s
  </ol>
  </body>
  </html>"""

class ListHandler(webapp.RequestHandler):
  def get(self):
    # Get the top 100 scores by game and optionally name.
    query = HighScore.all()
    game = self.request.get("game")
    query.filter("game =", game)
    user = self.request.get("user", None)
    if user:
      query.filter("user =", user)
    query.order("-score")
    highscores = query.fetch(100)
    highscores_html = [highscoreHtml(highscore) for highscore in highscores]
    self.response.out.write(
        _HTML_TEMPLATE % {"game": game,
                          "list": "".join(highscores_html)})

app = webapp.WSGIApplication([
    ('/nonce', NonceHandler),
    ('/submit', SubmitHandler),
    ('/list', ListHandler),
  ])

