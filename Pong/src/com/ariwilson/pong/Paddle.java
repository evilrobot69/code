package com.ariwilson.pong;

public abstract class Paddle extends GameObject {
  public Paddle(int x, int y) {
    // TODO(ariw): Adjust width/height based on screen size.
    height_ = 20;
    width_ = 5;

    x_ = x;
    y_ = y;
  }

  protected int height_;
  protected int width_;

  protected int x_;
  protected int y_;
}
