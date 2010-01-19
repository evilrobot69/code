package com.ariwilson.seismo;

public class AccelerometerReaderThread extends Thread {
  public AccelerometerReaderThread(AccelerometerReader reader,
                                   SeismoViewThread view, boolean paused,
                                   int period) {
    reader_ = reader;
    view_ = view;
    setPaused(paused);
    period_ = period;
  }

  @Override
  public void run() {
    while (running_) {
      if (displayable_ && !paused_) {
        view_.update(reader_.x, reader_.y, reader_.z);
      }
      try {
        Thread.sleep(period_);
      } catch (Exception e) {
        // Ignore.
      }
    }
  }
  
  public void setRunning(boolean running) {
    running_ = running;
  }

  public void setDisplayable(boolean displayable) {
    displayable_ = displayable;
  }

  public void setPaused(boolean paused) {
    paused_ = paused;
  }


  private boolean running_ = true;
  private boolean displayable_ = false;
  private boolean paused_ = false;
  private volatile AccelerometerReader reader_;
  private SeismoViewThread view_;
  private int period_;
}
