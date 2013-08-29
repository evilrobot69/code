#!/usr/bin/python


# Example usage:
# Fiji Way points:
# Start: 33.973542,-118.445449
# End: 33.977137,-118.438389
# Target speed: 30 mi/h
# Time: 1pm Saturday.
# Height: ~5m.
#
# ./strava_cheater.py 33.973542 -118.445449 33.977137 -118.438389 30 test.gpx

import math, sys

GPX_POINT = """<trkpt lat="%(latitude)f" lon="%(longitude)">
<ele>%(elevation)f</ele>
<time>%(time)s</time>
</trkpt>"""

GPX_TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<gpx
version="1.1"
creator="Created by Ari on A Computer."
xmlns="http://www.topografix.com/GPX/1/1"
xmlns:topografix="http://www.topografix.com/GPX/Private/TopoGrafix/0/1"
xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
xsi:schemaLocation="http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd http://www.topografix.com/GPX/Private/TopoGrafix/0/1 http://www.topografix.com/GPX/Private/TopoGrafix/0/1/topografix.xsd">
<metadata>
<name><![CDATA[Track]]></name>
<desc><![CDATA[]]></desc>
</metadata>
<trk>
<name><![CDATA[Track]]></name>
<desc><![CDATA[]]></desc>
<type><![CDATA[cycling]]></type>
<extensions><topografix:color>c0c0c0</topografix:color></extensions>
<trkseg>
%s
</trkseg>
</trk>
</gpx>"""

GPX_HEIGHT_M = 5

GPX_TIME = "2013-08-24T21:00:%s.000Z"

EARTH_RADIUS_MI = 3963.19

# Use Pythagorean theorem to get total distance assuming an equirectangular
# projection (an approximation that works on small distances).
def pythag_distance(start, end):
  # Convert to radians.
  start = (start[0] * math.pi / 180, start[1] * math.pi / 180)
  end = (end[0] * math.pi / 180, end[1] * math.pi / 180)

  # Convert to equirectangular projection, get Pythagorean distance.
  a = (end[1] - start[1]) * math.cos((start[0] + end[0]) / 2)
  b = (end[0] - start[0])
  return math.sqrt(a * a + b * b)

if __name__ == "__main__":
  assert len(sys.argv) == 7

  start = (float(sys.argv[1]), float(sys.argv[2]))
  end = (float(sys.argv[3]), float(sys.argv[4]))

  distance = pythag_distance(start, end) * EARTH_RADIUS_MI
  # Compute how many seconds we need in order to achieve target speed.
  speed_mi_h = float(sys.argv[5])
  speed_mi_s = speed_mi_h / (60 * 60)
  seconds_needed = distance / speed_mi_s

  # Convert start/end to some 2D projection, Pythagoras to get distance, invert to get
  # semi-evenly spaced points at desired speed between the two points.
  gpx_file = open(sys.argv[6], "w")
  points_gpx = ""
  elevation = GPX_HEIGHT_M
  for i in range(0, seconds_needed):
    if i < 10:
      seconds = "0" + str(i)
    else:
      seconds = str(i)
    time = GPX_TIME % (seconds)
    latitude = start + i *
    points_gpx += GPX_POINT % (locals())
  gpx_file.write(GPX_TEMPLATE % points_gpx)

