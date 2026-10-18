#pragma once
#include <string.h>
#include <math.h>
#include "quest_cockpit_fit.h"

/* Stock-car fit: measured driver position, windshield and body width, calibrated
 * to the shared Peugeot cabin dimensions (headset fit still needs verification). No retail mesh or texture is embedded here.
 * Profiles are immutable; personal height/distance remain XR eye offsets. */
typedef struct {
    const char *name;
    float seat[3];
    float near_width,far_width,front_depth,side,rear_limit;
} QuestCockpitVehicle;
static const QuestCockpitVehicle quest_cockpit_vehicles[]={
    {"DEFAULT",{-.10f,.35f,.95f},1,1,1,1,20},
    {"240SX",{0.028710f,0.387445f,0.783809f},0.895846f,1.024196f,0.745138f,1.000000f,20.000000f},
    {"350Z",{-0.243177f,0.435649f,0.897662f},0.965195f,1.135109f,0.924898f,1.000000f,0.460000f},
    {"CELICA",{-0.116100f,0.447042f,0.869052f},0.862711f,1.114711f,1.040088f,1.000000f,20.000000f},
    {"CIVIC",{0.026244f,0.386362f,0.890257f},0.958581f,1.046913f,0.801929f,1.000000f,20.000000f},
    {"ECLIPSE",{-0.066512f,0.447042f,0.858775f},0.836066f,1.104263f,0.949490f,1.000000f,20.000000f},
    {"FOCUS",{-0.078359f,0.438639f,0.994417f},0.842246f,1.092092f,1.013382f,1.000000f,20.000000f},
    {"GOLF",{-0.101768f,0.426007f,0.917057f},0.912806f,1.097821f,0.895367f,1.000000f,20.000000f},
    {"IMPREZA",{0.071930f,0.428578f,0.926136f},0.912432f,1.102139f,0.914258f,1.000000f,20.000000f},
    {"INTEGRA",{-0.107933f,0.391084f,0.894339f},1.012898f,1.076413f,0.914199f,1.000000f,20.000000f},
    {"LANCER",{0.233013f,0.454125f,0.848202f},0.786210f,1.097017f,0.684907f,1.000000f,20.000000f},
    {"MIATA",{-0.306802f,0.392122f,0.811262f},0.929337f,1.045450f,0.627145f,1.000000f,0.460000f},
    {"NEON",{-0.058940f,0.391120f,0.933639f},0.950701f,1.052088f,1.137194f,1.000000f,20.000000f},
    {"PEUGOT",{-0.100000f,0.350000f,0.950000f},1.000000f,1.000000f,1.000000f,1.000000f,20.000000f},
    {"RSX",{-0.034607f,0.397531f,0.922227f},0.969117f,1.070442f,0.922829f,1.000000f,20.000000f},
    {"RX7",{-0.249512f,0.398225f,0.811992f},0.987689f,1.078930f,0.770955f,1.000000f,20.000000f},
    {"S2000",{-0.245823f,0.427924f,0.895223f},0.936273f,1.110351f,0.740411f,1.000000f,0.460000f},
    {"SENTRA",{0.108678f,0.472644f,0.931158f},0.765325f,1.120991f,0.745197f,1.000000f,20.000000f},
    {"SKYLINE",{0.034015f,-0.424885f,0.908227f},0.994384f,1.127860f,0.761345f,-1.000000f,20.000000f},
    {"SUPRA",{-0.204307f,0.487662f,0.858775f},0.890471f,1.196146f,0.850148f,1.000000f,20.000000f},
    {"TIBURON",{-0.106063f,0.383905f,0.895097f},1.056506f,1.081044f,0.881364f,1.000000f,20.000000f},
};
enum { QUEST_COCKPIT_VEHICLES=sizeof quest_cockpit_vehicles/sizeof quest_cockpit_vehicles[0] };
static inline int quest_cockpit_vehicle_index(const char *name)
{
    if(name)for(int i=1;i<QUEST_COCKPIT_VEHICLES;++i)if(!strcmp(name,quest_cockpit_vehicles[i].name))return i;
    return 0;
}
static inline const QuestCockpitVehicle *quest_cockpit_vehicle(int index)
{ return &quest_cockpit_vehicles[index>0 && index<QUEST_COCKPIT_VEHICLES?index:0]; }

/* Static cabin and dashboard only: the wheel/hands keep their physical size.
 * The dash is fitted beyond the wheel plane. Mirrors stay on original glass. */
static inline void quest_cockpit_fit_point(float out[3],const float p[3],const QuestCockpitVehicle *fit)
{
    out[0]=p[0]*(p[0]<0?fit->near_width:fit->far_width)*fit->side;
    out[1]=p[1];
    out[2]=p[2]<-.33f?-.33f+(p[2]+.33f)*fit->front_depth:p[2];
}
