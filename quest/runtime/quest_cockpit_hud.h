#pragma once
#include "quest_cockpit_panels.h"
#include "quest_cockpit_vehicle.h"
#include "quest_minimap.h"

/* NFSU1's minimap sits halfway down the left edge. Its instrument cluster
 * includes the smaller auxiliary dial to the left of the tachometer. The NFSU2
 * split at y=260 / x=440 cuts both widgets across different physical planes.
 * Partition the entire 640x480 source once; pause menus remain unsplit. */
enum {
    QUEST_HUD_GAUGE, QUEST_HUD_MAP, QUEST_HUD_INFO_FIRST,
    QUEST_HUD_WINDOW = 7,
    QUEST_HUD_REGIONS = 8,
    QUEST_HUD_INSTRUMENT_VERTICES = 12,
    QUEST_HUD_INFO_VERTICES = 30,
    QUEST_HUD_MENU_FIRST = 48,
    QUEST_HUD_PANEL_VERTICES = 54
};
typedef struct { float x0,y0,x1,y1; } QuestHudRect;
static const QuestHudRect quest_hud_regions[QUEST_HUD_REGIONS] = {
    {360,280,640,480}, /* complete tachometer, turbo, gear and speed */
    {0,176,144,328},   /* complete minimap, including the top and route arrow */
    {184,0,640,120},
    {0,120,640,176},
    {144,176,640,280},
    {144,280,360,328},
    {0,328,360,480},
    {0,0,184,120} /* NFSU1 lap count plus current and best times */
};

static inline void quest_cockpit_hud_panels(QuestGhostVertex out[QUEST_HUD_PANEL_VERTICES],
                                           const QuestCockpitVehicle *fit)
{
    QuestHudRect gauge=quest_hud_regions[QUEST_HUD_GAUGE];
    /* Preserve pixel aspect and keep the large dial centered behind the
     * wheel, with room for the smaller auxiliary dial on its left. */
    quest_cockpit_panel(out,-.054f,-.205f,-.66f,.30f,.30f*200/280,
                        gauge.x0,gauge.y0,gauge.x1,gauge.y1);
    /* The map shader crops the retail HUD. Draw and grip use the same
     * per-car mount to place this rigid face in the cabin. */
    quest_cockpit_panel(out+6,0,0,0,2*QUEST_MINIMAP_RADIUS,2*QUEST_MINIMAP_RADIUS,0,0,640,480);
    for(int region=QUEST_HUD_INFO_FIRST;region<QUEST_HUD_WINDOW;++region) {
        QuestHudRect r=quest_hud_regions[region];
        quest_cockpit_panel(out+region*6,((r.x0+r.x1)/1280-.5f)*2.8f,
            1.45f-(r.y0+r.y1)/960*1.7f,-3,(r.x1-r.x0)/640*2.8f,(r.y1-r.y0)/480*1.7f,
            r.x0,r.y0,r.x1,r.y1);
    }
    QuestGhostVertex *window=out+QUEST_HUD_WINDOW*6;
    int right_hand_drive=fit->side<0;
    float metres_per_pixel=right_hand_drive?.00080f:.00140f;
    quest_cockpit_panel(window,0,0,0,1,1,0,0,184,120);
    for(int i=0;i<6;++i) {
        float u=640*window[i].uv[0],v=480*window[i].uv[1];
        float y=(right_hand_drive?-.020f:.003f)+(60-v)*metres_per_pixel;
        /* Inside the right glass, with text running toward the rear (+Z)
         * when viewed from the seat. Right-hand-drive cars use the near pane. */
        window[i].position[0]=fit->right_window_x-.025f-y*fit->right_window_lean;
        window[i].position[1]=y;
        window[i].position[2]=(right_hand_drive?-.110f:-.038f)+(u-92)*metres_per_pixel;
    }
    quest_cockpit_menu_panel(out+QUEST_HUD_MENU_FIRST);
    for(int v=0;v<6;++v) {
        float position[3];quest_cockpit_fit_point(position,out[v].position,fit);
        memcpy(out[v].position,position,sizeof position);
        if(fit->side<0) {
            out[v].uv[0]=(gauge.x0+gauge.x1)/640-out[v].uv[0];
        }
    }
}
