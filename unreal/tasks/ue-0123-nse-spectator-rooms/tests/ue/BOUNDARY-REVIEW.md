# Delay-boundary before-state witness

Protected SixProcessAuthority feedback shows frame 163 confirmed at tick 303. The test advances to tick 422, exactly t+120−1, and requests frame 163. The authority returns a rejected seek at frame 162/edge 162. A preceding unsolicited delivery legitimately advances the live cursor from its formerly cached frame 160 to newly released frame 162. The original unchanged-state check compared that rejection against the old cached 160 and failed even though frame 163 remained withheld.

The boundary request now takes a correlated read-only observation immediately before the seek. It still requires rejection, meaningful diagnostic and unchanged current state, and explicitly requires both displayed frame and released edge to remain below the requested frame. The healthy permitted-history request and exact t+D release assertion remain. No clock, confirmation timestamp, delay, or deadline is changed.

Python compilation and all 14 host tests pass. Protected engine rerun remains necessary.
