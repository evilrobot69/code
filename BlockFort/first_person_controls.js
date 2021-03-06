/**
 * @author mrdoob / http://mrdoob.com/
 * @author ariwilson / http://www.ariwilson.com/
 */

THREE.FirstPersonControls = function ( camera, kbm ) {

  var enabled = false;

  var pitchObject = new THREE.Object3D();
  pitchObject.add( camera );

  var yawObject = new THREE.Object3D();
  yawObject.position.y = 10;
  yawObject.add( pitchObject );

  var moveForward = false;
  var moveBackward = false;
  var moveLeft = false;
  var moveRight = false;
  var flyUp = false;
  var flyDown = false;

  var PI_2 = Math.PI / 2;

  var look = function(movementX, movementY) {
    if ( this.enabled === false ) return;

    yawObject.rotation.y -= movementX * 0.002;
    pitchObject.rotation.x -= movementY * 0.002;

    pitchObject.rotation.x = Math.max( - PI_2, Math.min(
        PI_2, pitchObject.rotation.x ) );
  };

  var onMouseMove = function ( event ) {
    var movementX = event.movementX || event.mozMovementX ||
                    event.webkitMovementX || 0;
    var movementY = event.movementY || event.mozMovementY ||
                    event.webkitMovementY || 0;
    look(movementX, movementY);
  };

  var onKeyDown = function ( event ) {
    switch ( event.keyCode ) {

      case 38: // up
      case 87: // w
        moveForward = true;
        break;

      case 37: // left
      case 65: // a
        moveLeft = true;
        break;

      case 40: // down
      case 83: // s
        moveBackward = true;
        break;

      case 39: // right
      case 68: // d
        moveRight = true;
        break;

      case 32: // space
        flyUp = true;
        break;
      case 16: // shift
        flyDown = true;
        break;

    }

  };

  var onKeyUp = function ( event ) {
    switch( event.keyCode ) {

      case 38: // up
      case 87: // w
        moveForward = false;
        break;

      case 37: // left
      case 65: // a
        moveLeft = false;
        break;

      case 40: // down
      case 83: // a
        moveBackward = false;
        break;

      case 39: // right
      case 68: // d
        moveRight = false;
        break;

      case 32: // space
        flyUp = false;
        break;
      case 16: // shift
        flyDown = false;
        break;
    }

  };

  var isMoveTouch = function(x, y) {
    return y >= window.innerHeight / 2 && x <= window.innerWidth / 3;
  };

  var isHeightTouch = function(x, y) {
    return y <= window.innerHeight / 5 &&
           (x <= window.innerWidth / 3 || x >= 2 * window.innerWidth / 3);
  };

  var isLookTouch = function(x, y) {
    return y >= window.innerHeight / 2 && x >= 2 * window.innerWidth / 3;
  };

  var moveTouches = {};
  var lookTouches = {};
  var createTouches = {};

  var onTouchStart = function(event) {
    var width = window.innerWidth;
    var height = window.innerHeight;
    for (var i = 0; i < event.changedTouches.length; i++) {
      var touch = event.changedTouches[i];
      if (isMoveTouch(touch.pageX, touch.pageY)) {
        var relativeWidth = width / 3;
        var relativeHeight = height / 2;
        var relativeX = touch.pageX - relativeWidth / 2;
        var relativeY = touch.pageY - height / 2 - relativeHeight / 2;
        if (relativeX <= relativeY) {
          if (relativeX <= -relativeY) {
            moveTouches[touch.identifier] = 1;
            moveLeft = true;
          } else {
            moveTouches[touch.identifier] = 2;
            moveBackward = true;
          }
        } else {
          if (relativeX <= -relativeY) {
            moveTouches[touch.identifier] = 3;
            moveForward = true;
          } else {
            moveTouches[touch.identifier] = 4;
            moveRight = true;
          }
        }
      } else if (isHeightTouch(touch.pageX, touch.pageY)) {
        if (touch.pageX <= width / 3) {
          moveTouches[touch.identifier] = 5;
          flyDown = true;
        } else if (touch.pageX >= 2 * width / 3) {
          moveTouches[touch.identifier] = 6;
          flyUp = true;
        }
      } else if (isLookTouch(touch.pageX, touch.pageY)) {
        lookTouches[touch.identifier] = [touch.pageX, touch.pageY];
      } else {
        createTouches[touch.identifier] = new Date().getTime();
      }
      event.preventDefault();
    }
  };

  var onTouchMove = function(event) {
    event.preventDefault();
    for (var i = 0; i < event.changedTouches.length; i++) {
      var touch = event.changedTouches[i];
      if (touch.identifier in lookTouches) {
        look(2 * (touch.pageX - lookTouches[touch.identifier][0]),
             2 * (touch.pageY - lookTouches[touch.identifier][1]));
        lookTouches[touch.identifier] = [touch.pageX, touch.pageY];
      }
    }
  };

  var onTouchEnd = function(event) {
    for (var i = 0; i < event.changedTouches.length; i++) {
      var touch = event.changedTouches[i];
      if (touch.identifier in moveTouches) {
        switch (moveTouches[touch.identifier]) {
          case 1:
            moveLeft = false;
            break;
          case 2:
            moveBackward = false;
            break;
          case 3:
            moveForward = false;
            break;
          case 4:
            moveRight = false;
            break;
          case 5:
            flyDown = false;
            break;
          case 6:
            flyUp = false;
            break;
        }
        delete moveTouches[touch.identifier];
      } else if (touch.identifier in lookTouches) {
        delete lookTouches[touch.identifier];
      } else if (touch.identifier in createTouches) {
        var ev = new Object();
        if (new Date().getTime() - createTouches[touch.identifier] < 500) {
          ev.which = 1;
        } else {
          ev.which = 3;
        }
        blockfort.buildClick(ev);
        delete createTouches[touch.identifier];
      }
    }
  };

  this.getObject = function () {

    return yawObject;

  };

  this.getDirection = function() {

    // assumes the camera itself is not rotated

    var direction = new THREE.Vector3( 0, 0, -1 );
    var rotation = new THREE.Euler( 0, 0, 0, "YXZ" );

    return function( v ) {

      rotation.set( pitchObject.rotation.x, yawObject.rotation.y, 0 );

      v.copy( direction ).applyEuler( rotation );

      return v;

    }

  }();

  this.update = function ( delta ) {

    if ( this.enabled === false ) return;

    var velocity = 0.128;
    if ( moveForward ) yawObject.translateZ( -velocity * delta);
    if ( moveBackward ) yawObject.translateZ( velocity * delta);

    if ( flyDown ) yawObject.translateY( -velocity * delta);
    if ( flyUp ) yawObject.translateY( velocity * delta);

    if ( moveLeft ) yawObject.translateX( -velocity * delta);
    if ( moveRight ) yawObject.translateX( velocity * delta);


    if ( yawObject.position.y < 32 ) {

      yawObject.position.y = 32;

    }

  };

  this.connect = function() {
    document.addEventListener( 'click', blockfort.buildClick, false );
    if (kbm) {
      document.addEventListener( 'mousemove', onMouseMove, false );
      document.addEventListener( 'keydown', onKeyDown, false );
      document.addEventListener( 'keyup', onKeyUp, false );
    } else {
      document.addEventListener( 'touchstart', onTouchStart, false);
      document.addEventListener( 'touchmove', onTouchMove, false);
      document.addEventListener( 'touchend', onTouchEnd, false);
    }
    this.enabled = true;
  }

  this.disconnect = function() {
    this.enabled = false;
    document.removeEventListener( 'click', blockfort.buildClick, false);
    if (kbm) {
      document.removeEventListener( 'mousemove', onMouseMove, false );
      document.removeEventListener( 'keydown', onKeyDown, false );
      document.removeEventListener( 'keyup', onKeyUp, false );
    } else {
      document.removeEventListener( 'touchstart', onTouchStart, false);
      document.removeEventListener( 'touchmove', onTouchMove, false);
      document.removeEventListener( 'touchend', onTouchEnd, false);
    }
  }

};
