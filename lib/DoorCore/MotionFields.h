#pragma once
#define MOTION_FIELDS(X) \
 X(travel) X(openSpeed) X(reopenSpeed) X(closeSpeed) X(homingSpeed) \
 X(openAccel) X(openDecel) X(closeAccel) X(closeDecel) X(homingAccel) X(homingDecel) \
 X(openCurrent) X(closeCurrent) X(homingCurrent) X(dwellMs) X(shortDwellMs) X(settleMs) \
 X(openTimeoutMs) X(closeTimeoutMs) X(homingTimeoutMs) X(closeDrift) X(openDrift)
