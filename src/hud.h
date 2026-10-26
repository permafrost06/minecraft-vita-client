#ifndef MC_HUD_H
#define MC_HUD_H
#include "reader.h"
/* Server-driven overlays: boss bars, titles, scoreboards, teams and tab list
   text. Text is plain ASCII; formatting and colours are dropped. */
#define MC_HUD_TEXT 128
#define MC_HUD_BOSSES 8
#define MC_HUD_OBJECTIVES 16
#define MC_HUD_SCORES 256
#define MC_HUD_TEAMS 64
#define MC_HUD_MEMBERS 256
#define MC_HUD_SIDEBAR 15
typedef struct {int active;uint8_t uuid[16];char title[MC_HUD_TEXT];float health;int color,division,flags;} McBossBar;
typedef struct {int active;char name[17];char display[MC_HUD_TEXT];int type;} McObjective;
typedef struct {int active;char entity[41];char objective[17];int value;} McScore;
typedef struct {int active;char name[17];char prefix[64],suffix[64];int color;} McTeam;
typedef struct {int active,team;char name[41];} McTeamMember;
typedef struct {
    McBossBar bosses[MC_HUD_BOSSES];
    char title[MC_HUD_TEXT],subtitle[MC_HUD_TEXT];
    int fade_in,stay,fade_out;
    uint64_t title_at;
    McObjective objectives[MC_HUD_OBJECTIVES];
    char display[19][17];
    McScore scores[MC_HUD_SCORES];
    McTeam teams[MC_HUD_TEAMS];
    McTeamMember members[MC_HUD_MEMBERS];
    char header[256],footer[256],motd[MC_HUD_TEXT];
    int difficulty,difficulty_locked,secure_chat;
    uint64_t updates,overflow;
    char error[96];
} McHud;
typedef struct {char text[MC_HUD_TEXT];int value;} McSidebarLine;
typedef struct {char title[MC_HUD_TEXT];unsigned count;McSidebarLine lines[MC_HUD_SIDEBAR];} McSidebar;
void mc_hud_init(McHud *hud);
/* Returns -1 for malformed packets. Join Game and Respawn clear titles;
   Join Game also clears scoreboards and boss bars. */
int mc_hud_packet(McHud *hud,uint32_t id,const uint8_t *p,size_t n,uint64_t now);
/* Title opacity in [0,1]; 0 when no title is visible. */
float mc_hud_title_alpha(const McHud *hud,uint64_t now);
/* Sorted sidebar (highest score first) with team prefixes and suffixes applied.
   team_color selects a per-team sidebar slot (-1 for none). Returns 0 if hidden. */
int mc_hud_sidebar(const McHud *hud,int team_color,McSidebar *out);
/* Team colour (0-21) of a player or score holder, or -1 without a team. */
int mc_hud_team_color(const McHud *hud,const char *name);
/* Display name with team prefix/suffix for a player or score holder. */
void mc_hud_decorated(const McHud *hud,const char *name,char *out,size_t capacity);
#endif
