#pragma once
#include "quest_cockpit_panels.h"
#include "quest_cockpit_vehicle.h"

/* NFSU1's minimap sits halfway down the left edge. Its instrument cluster
 * includes the smaller auxiliary dial to the left of the tachometer. The NFSU2
 * split at y=260 / x=440 cuts both widgets across different physical planes.
 * Partition the entire 640x480 source once; pause menus remain unsplit. */
enum {
    QUEST_HUD_GAUGE, QUEST_HUD_MAP, QUEST_HUD_INFO_FIRST,
    QUEST_HUD_REGIONS = 6,
    QUEST_HUD_INSTRUMENT_VERTICES = 12,
    QUEST_HUD_INFO_VERTICES = 24,
    QUEST_HUD_MENU_FIRST = 36,
    QUEST_HUD_PANEL_VERTICES = 42
};
typedef struct { float x0,y0,x1,y1; } QuestHudRect;
static const QuestHudRect quest_hud_regions[QUEST_HUD_REGIONS] = {
    {360,280,640,480}, /* complete tachometer, turbo, gear and speed */
    {0,176,144,328},   /* complete minimap, including the top and route arrow */
    {0,0,640,176},
    {144,176,640,280},
    {144,280,360,328},
    {0,328,360,480}
};

static inline void quest_cockpit_hud_panels(QuestGhostVertex out[QUEST_HUD_PANEL_VERTICES],
                                           const QuestCockpitVehicle *fit)
{
    QuestHudRect gauge=quest_hud_regions[QUEST_HUD_GAUGE],map=quest_hud_regions[QUEST_HUD_MAP];
    /* Preserve pixel aspect and keep the large dial centered behind the
     * wheel, with room for the smaller auxiliary dial on its left. */
    quest_cockpit_panel(out,-.054f,-.205f,-.66f,.30f,.30f*200/280,
                        gauge.x0,gauge.y0,gauge.x1,gauge.y1);
    /* Measured flat section of the accepted console, between the vents and
     * gear lever. A 16x17 cm display follows its slope and per-car fit. */
    quest_cockpit_panel(out+6,.3116f,-.31f,-.412f,.17f*144/152,.17f,
                        map.x0,map.y0,map.x1,map.y1);
    for(int v=6;v<12;++v) {
        out[v].position[2]-=.50f*(out[v].position[1]+.31f);
        out[v].normal[1]=.44721360f;out[v].normal[2]=.89442719f;
    }
    for(int region=QUEST_HUD_INFO_FIRST;region<QUEST_HUD_REGIONS;++region) {
        QuestHudRect r=quest_hud_regions[region];
        quest_cockpit_panel(out+region*6,((r.x0+r.x1)/1280-.5f)*2.8f,
            1.45f-(r.y0+r.y1)/960*1.7f,-3,(r.x1-r.x0)/640*2.8f,(r.y1-r.y0)/480*1.7f,
            r.x0,r.y0,r.x1,r.y1);
    }
    quest_cockpit_menu_panel(out+QUEST_HUD_MENU_FIRST);
    for(int v=0;v<QUEST_HUD_INSTRUMENT_VERTICES;++v) {
        float position[3];quest_cockpit_fit_point(position,out[v].position,fit);
        memcpy(out[v].position,position,sizeof position);
        if(fit->side<0) {
            QuestHudRect r=quest_hud_regions[v/6];
            out[v].uv[0]=(r.x0+r.x1)/640-out[v].uv[0];
        }
    }
}
