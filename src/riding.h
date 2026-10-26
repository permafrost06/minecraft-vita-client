#ifndef MC_RIDING_H
#define MC_RIDING_H
#include "gameplay.h"
/* Tick and send mounted movement. Returns 1 if mounted, 0 if walking, -1 on send error.
   Caller serializes entities/world/player access. Boats are locally simulated only
   for their first passenger; other passengers follow server entity movement. */
int mc_riding_tick(McClient *c,McEntities *entities,McPlayer *p,const McWorld *w,const McInput *input);
#endif
