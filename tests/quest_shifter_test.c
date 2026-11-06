#include "../quest/runtime/quest_shifter.h"
#include "../quest/runtime/quest_pedals.h"
#include <stdio.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x); return 1; } } while(0)

/* Independent expected positions, in units of rail pitch and lever throw.
 * Array order is the retail ID order R,N,1,2,3,4,5,6. */
static const int gate5[][2]={{1,1},{0,0},{-1,-1},{-1,1},{0,-1},{0,1},{1,-1}};
static const int gate6[][2]={{2,1},{0,0},{-1,-1},{-1,1},{0,-1},{0,1},{1,-1},{1,1}};

static int shifts_between_slots(int count,int vehicle,int hand)
{
    const int (*gate)[2]=count==6?gate6:gate5;
    for(int from=0;from<count+2;++from)for(int to=0;to<count+2;++to) {
        QuestShifter s;quest_shifter_reset(&s);
        int valid[2]={1,1},wheel[2]={0,0};wheel[1-hand]=1;
        float squeeze[2]={0,0},pos[2][3]={{0}},center[3];
        quest_shifter_knob(&s,vehicle,center);
        memcpy(pos[hand],center,sizeof center);
        pos[hand][0]+=gate[from][0]*QUEST_SHIFT_PITCH+.012f;
        pos[hand][2]+=gate[from][1]*QUEST_SHIFT_THROW+.008f;
        CHECK(quest_shifter_update(&s,0,vehicle,count,from,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        squeeze[0]=squeeze[1]=1;
        CHECK(quest_shifter_update(&s,10,vehicle,count,from,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        CHECK(s.hand==hand+1 && s.gear==from);
        pos[hand][2]=center[2]+.008f;
        CHECK(quest_shifter_update(&s,20,vehicle,count,from,valid,pos,squeeze,wheel)==
            (from==QUEST_SHIFT_N?QUEST_SHIFT_NONE:QUEST_SHIFT_N));
        /* Slide in samples through all intermediate lanes without shifting. */
        for(int i=1;i<=6;++i) {
            float lane=gate[from][0]+(gate[to][0]-gate[from][0])*i/6.f;
            pos[hand][0]=center[0]+lane*QUEST_SHIFT_PITCH+.012f;
            CHECK(quest_shifter_update(&s,20+i,vehicle,count,QUEST_SHIFT_N,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        }
        pos[hand][2]+=gate[to][1]*QUEST_SHIFT_THROW;
        CHECK(quest_shifter_update(&s,35,vehicle,count,QUEST_SHIFT_N,valid,pos,squeeze,wheel)==
            (to==QUEST_SHIFT_N?QUEST_SHIFT_NONE:to));
        CHECK(s.gear==to);
        CHECK(quest_shifter_update(&s,45,vehicle,count,to,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        valid[hand]=0;squeeze[hand]=0;
        CHECK(quest_shifter_update(&s,55,vehicle,count,to,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        CHECK(!s.hand && s.gear==to);
    }
    return 0;
}

static int shifter_reach_test(void)
{
    for(int vehicle=0;vehicle<QUEST_COCKPIT_VEHICLES;++vehicle)for(int h=0;h<2;++h) {
        QuestShifter s;quest_shifter_reset(&s);
        int valid[2]={0},busy[2]={0};
        float pos[2][3]={{0}},squeeze[2]={0};
        int gear=QUEST_SHIFT_FIRST;
        quest_shifter_detent(&s,gear,5);
        quest_shifter_knob(&s,vehicle,pos[h]);valid[h]=1;
        pos[h][1]+=.066f;
        quest_shifter_update(&s,0,vehicle,5,gear,valid,pos,squeeze,busy);
        CHECK(!s.at_lever && !s.hand);
        pos[h][1]-=.002f;busy[h]=1;
        quest_shifter_update(&s,1,vehicle,5,gear,valid,pos,squeeze,busy);
        CHECK(!s.at_lever && !s.hand);
        busy[h]=0;
        quest_shifter_update(&s,2,vehicle,5,gear,valid,pos,squeeze,busy);
        CHECK(s.at_lever && !s.hand && s.gear==gear);
        squeeze[h]=1;
        quest_shifter_update(&s,3,vehicle,5,gear,valid,pos,squeeze,busy);
        CHECK(s.at_lever && s.hand==h+1);
        pos[h][1]+=.02f; /* Still holding the knob outside the pickup radius. */
        quest_shifter_update(&s,4,vehicle,5,gear,valid,pos,squeeze,busy);
        CHECK(s.at_lever && s.hand==h+1);
        pos[h][1]-=.02f;squeeze[h]=0;
        quest_shifter_update(&s,5,vehicle,5,gear,valid,pos,squeeze,busy);
        CHECK(s.at_lever && !s.hand);
        valid[h]=0;
        quest_shifter_update(&s,6,vehicle,5,gear,valid,pos,squeeze,busy);
        CHECK(!s.at_lever);
        valid[h]=1;pos[h][1]+=.002f;
        quest_shifter_update(&s,7,vehicle,5,gear,valid,pos,squeeze,busy);
        CHECK(!s.at_lever);
    }
    return 0;
}

int main(void)
{
    /* The start film and countdown can skip a director stage. The selected
     * gear belongs to the local car until the starting signal. */
    for (int manual=0;manual<=1;++manual) {
        QuestGridShifter grid={0};
        CHECK(!quest_grid_stage(&grid,7,4,0,5,manual));
        CHECK(quest_grid_request(&grid,7,1)==QUEST_SHIFT_NONE);
        CHECK(quest_grid_stage(&grid,7,1,0,5,manual));
        CHECK(quest_grid_request(&grid,7,1)==(manual?QUEST_SHIFT_N:QUEST_SHIFT_NONE));
        QuestShifter lever;quest_shifter_reset(&lever);lever.first_only=1;
        grid.gear=quest_shifter_move(&lever,-QUEST_SHIFT_PITCH,-QUEST_SHIFT_THROW,5);
        grid.selected=1;
        CHECK(!quest_grid_stage(&grid,7,2,1,5,manual));
        CHECK(!quest_grid_stage(&grid,7,3,2,5,manual));
        CHECK(quest_grid_request(&grid,7,1)==QUEST_SHIFT_FIRST && !grid.racing);
        CHECK(quest_grid_request(&grid,8,1)==QUEST_SHIFT_NONE);
        CHECK(!quest_grid_stage(&grid,7,4,3,5,manual));
        CHECK(grid.racing && quest_grid_request(&grid,7,1)==QUEST_SHIFT_FIRST);
        for (int lock=QUEST_DRIVER_READY;lock<=QUEST_DRIVER_NO_INPUT;++lock) {
            CHECK(quest_grid_driver(&grid,7,1,QUEST_SHIFT_N,lock)==QUEST_SHIFT_FIRST && grid.car==7);
        }
        CHECK(quest_grid_driver(&grid,7,1,QUEST_SHIFT_FIRST,QUEST_DRIVER_READY)==
              (manual?QUEST_SHIFT_FIRST:QUEST_SHIFT_RESUME_AUTO));
        CHECK(!grid.car);
        CHECK(quest_grid_stage(&grid,7,3,4,5,manual) && !grid.selected && !grid.racing);
        CHECK(quest_grid_request(&grid,7,0)==QUEST_SHIFT_NONE); /* untouched gamepad */
        unsigned consumed=0;
        CHECK(quest_grid_buttons(&grid,1,1,&consumed)==1);
        CHECK(quest_grid_request(&grid,7,0)==QUEST_SHIFT_FIRST);
        CHECK(!quest_grid_buttons(&grid,1,0,&consumed));
        CHECK(quest_grid_buttons(&grid,1,2,&consumed)==2);
        CHECK(quest_grid_request(&grid,7,0)==QUEST_SHIFT_N);
        CHECK(quest_grid_buttons(&grid,1,1,&consumed)==1);
        CHECK(quest_grid_stage(&grid,0,0,3,0,manual) && !grid.car);
        CHECK(quest_grid_buttons(&grid,0,1,&consumed)==1); /* same pulse straddles GO */
        CHECK(!quest_grid_buttons(&grid,0,0,&consumed));
        CHECK(!quest_grid_buttons(&grid,0,1,&consumed)); /* next press goes to retail */
    }
    CHECK(!shifter_reach_test());
    QuestPedals pedals={0};
    for(int i=0;i<256;++i) {
        quest_pedals_update(&pedals,i,1,i,255-i,1,2,0,0);
        CHECK(fabsf(pedals.travel[0]-i/255.f)<.00001f);
        CHECK(fabsf(pedals.travel[1]-(255-i)/255.f)<.00001f && pedals.travel[2]==0);
    }
    /* Hold neutral through a manual shift, seat the next gear and leave
     * the hand resting on the lever. The clutch must return on its own. */
    for(unsigned t=270;t<=470;t+=10)quest_pedals_update(&pedals,t,1,90,190,1,1,1,t==270);
    CHECK(pedals.travel[2]==1);
    for(unsigned t=480;t<=850;t+=10)quest_pedals_update(&pedals,t,1,90,190,1,4,0,t==480);
    CHECK(pedals.travel[2]==0);
    quest_pedals_update(&pedals,866,1,0,255,1,5,0,0); /* sequential shift */
    CHECK(pedals.travel[2]>0 && pedals.travel[0]==0 && pedals.travel[1]==1);
    quest_pedals_update(&pedals,880,1,255,255,1,-1,0,0); /* automatic */
    CHECK(pedals.travel[2]==0 && pedals.travel[0]==1 && pedals.travel[1]==1);
    quest_pedals_update(&pedals,900,0,255,255,1,3,1,1);
    CHECK(!pedals.travel[0] && !pedals.travel[1] && !pedals.travel[2]);
    puts("Pedals passed all trigger levels, independent inputs and timed clutch engagement/release.");
    for(int count=4;count<=6;++count)for(int hand=0;hand<2;++hand) {
        CHECK(!shifts_between_slots(count,quest_cockpit_vehicle_index("PEUGOT"),hand));
        CHECK(!shifts_between_slots(count,quest_cockpit_vehicle_index("SKYLINE"),hand));
    }
    QuestShifter s;quest_shifter_reset(&s);
    s.first_only=1;
    CHECK(quest_shifter_move(&s,0,-QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE && s.gear==QUEST_SHIFT_N);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE && s.gear==QUEST_SHIFT_N);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,-.058f,6)==QUEST_SHIFT_NONE && s.gear==QUEST_SHIFT_N);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,-QUEST_SHIFT_THROW,6)==QUEST_SHIFT_FIRST);
    quest_shifter_reset(&s);
    /* Far-right forward is a blind end on a six-speed. Halfway between
     * lanes is also blocked, and lateral pressure cannot change an engaged gear. */
    CHECK(quest_shifter_move(&s,2*QUEST_SHIFT_PITCH,-QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE && s.z==0);
    CHECK(quest_shifter_move(&s,.5f*QUEST_SHIFT_PITCH,QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE && s.z==0);
    CHECK(quest_shifter_move(&s,0,-.030f,6)==QUEST_SHIFT_NONE);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,-.040f,6)==QUEST_SHIFT_NONE && s.x==0);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,-QUEST_SHIFT_THROW,6)==4);
    quest_shifter_detent(&s,2,6);
    CHECK(quest_shifter_move(&s,QUEST_SHIFT_PITCH,-QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE);
    CHECK(s.x==-QUEST_SHIFT_PITCH && s.gear==2);

    float pos[2][3]={{0}},squeeze[2]={1,0};
    int valid[2]={1,1},wheel[2]={0};
    quest_shifter_reset(&s);quest_shifter_knob(&s,0,pos[0]);
    quest_shifter_update(&s,0,0,6,1,valid,pos,squeeze,wheel);CHECK(!s.hand);
    squeeze[0]=0;quest_shifter_update(&s,5,0,6,1,valid,pos,squeeze,wheel);
    pos[0][1]+=.15f;squeeze[0]=1;
    quest_shifter_update(&s,10,0,6,1,valid,pos,squeeze,wheel);CHECK(!s.hand);
    pos[0][1]-=.15f;wheel[0]=1;
    quest_shifter_update(&s,15,0,6,1,valid,pos,squeeze,wheel);CHECK(!s.hand);
    wheel[0]=0;quest_shifter_update(&s,20,0,6,1,valid,pos,squeeze,wheel);CHECK(s.hand==1);
    valid[0]=0;pos[0][2]+=.10f;
    quest_shifter_update(&s,25,0,6,1,valid,pos,squeeze,wheel);CHECK(s.hand==1);
    valid[0]=1;CHECK(quest_shifter_update(&s,30,0,6,1,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE && s.z==0);
    squeeze[0]=NAN;quest_shifter_update(&s,540,0,6,1,valid,pos,squeeze,wheel);CHECK(!s.hand && s.blocked[0]);
    puts("H shifter passed all slot pairs, driver sides, either hand, neutral and tracking resets.");
    return 0;
}
