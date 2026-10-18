#include "../src/nfsu1_vr.h"
#include "../src/nfsu1_host_image.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } ++checks; } while (0)
static unsigned checks;
static unsigned char memory[0x300000];
static void put(uint32_t address, uint32_t value) { memcpy(memory+address,&value,4); }
static int read_memory(void *unused,uint32_t address,void *out,size_t size)
{
    (void)unused;
    if ((uint64_t)address+size>sizeof memory) return 0;
    memcpy(out,memory+address,size);return 1;
}
static void package(uint32_t node,const char *name,uint32_t next)
{
    put(node+4,next);put(node+0xc,node+0x100);
    strcpy((char *)memory+node+0x100,name);
}
int main(void)
{
    int paused;
    put(NFSU1_FRONTEND,0);
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==0 && !paused);
    put(NFSU1_FRONTEND,0x20000);put(0x20008,0x21000);put(0x210e4,0);
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==0 && !paused);
    put(0x210e4,0x10000);
    /* Reproduce the live Quest pause chain, including its controller mask 4.
     * This failed with the previous registry/bit-0 visibility assumption. */
    package(0x10000,"PreRaceStats.fng",0x11000);
    package(0x11000,"HUD_SingleRace.fng",0x12000);
    package(0x12000,"MU_Pause.fng",0);put(0x1201c,4);
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==1 && paused);
    put(0x1201c,0); /* input disabled does not remove a drawn menu */
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==1 && paused);
    put(0x11004,0); /* pause still cached, removed from active chain */
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==0 && !paused);
    put(0x1200c,0xfffffff0); /* released cached package is never followed */
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==0 && !paused);
    put(0x11004,0x12000);package(0x12000,"MU_Pause.fng",0x13000);
    package(0x13000,"MU_Options.fng",0);
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==1 && !paused);
    package(0x13000,"MU_Pause.fng.dialog",0); /* exact name only */
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==1 && !paused);
    package(0x13000,"Unknown.fng",0); /* unknown overlay blocks X */
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==1 && !paused);
    package(0x13000,"hud_drag.fng",0);
    package(0x12000,"mu_pause.FNG",0x13000);
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==1 && paused);
    put(0x13004,0x10000); /* corrupt cycle */
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==-1 && !paused);
    put(0x13004,0);put(0x1300c,0xfffffff0);
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==-1 && !paused);
    put(0x1300c,0x13100);memset(memory+0x13100,'x',64);
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==-1 && !paused);
    memory[0x13100]=0;
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==-1 && !paused);
    put(0x20008,0xfffffff0);
    CHECK(nfsu1_vr_menu(read_memory,0,&paused)==-1 && !paused);
    CHECK(!nfsu1_address(0xfffffff0,64));
    CHECK(!nfsu1_address(0x3fffff0,64));
    CHECK(nfsu1_address(0x83fffff0,16));

    /* Live view-list/manager layout captured during the failing pause:
     * driving mover still attached, manager+288 and rear-view list empty.
     * Exercise repeated pause/resume, restart, teardown and unrelated views. */
    const uint32_t mover=0x24000, manager=0x25000, camera=0x26000;
    Nfsu1PlayerCamera player;
    put(NFSU1_FLOW,6);put(NFSU1_VIEW(1)+4,1);
    put(NFSU1_VIEW(1)+0x40,camera);put(NFSU1_VIEW(1)+0x44,mover+4);
    put(mover,NFSU1_CAMERA_MOVER_VTABLE);put(mover+0x10,1);
    put(mover+0x18,NFSU1_VIEW(1));put(mover+0x1c,camera);
    put(mover+0x20,manager);put(mover+0x2c,0x23000);
    put(manager+4,0x23000);put(manager+0x10,0);
    put(manager+0x18,1);put(manager+0x1c,3);put(manager+0x27c,mover);
    for (unsigned repeat=0;repeat<3;++repeat) {
        put(manager+0x288,0);put(NFSU1_VIEW(3)+0x44,NFSU1_VIEW(3)+0x44);
        CHECK(nfsu1_player_camera(read_memory,0,&player));
        CHECK(player.camera==camera && player.car==0x23000 && player.manager==manager);
        CHECK(!player.rear && player.rear_view_empty);
        put(manager+0x288,0x27000);put(NFSU1_VIEW(3)+0x44,0x27004);
        CHECK(nfsu1_player_camera(read_memory,0,&player));
        CHECK(player.rear==0x27000 && !player.rear_view_empty);
    }
    put(manager+0x288,0); /* foreign rear view must not be replaced */
    CHECK(nfsu1_player_camera(read_memory,0,&player) && !player.rear_view_empty);
    put(mover+0x2c,0x28000); /* restart has replaced the car */
    CHECK(!nfsu1_player_camera(read_memory,0,&player) && !player.car);
    put(manager+4,0x28000);
    CHECK(nfsu1_player_camera(read_memory,0,&player) && player.car==0x28000);
    put(manager+0x10,1); /* second player */
    CHECK(!nfsu1_player_camera(read_memory,0,&player));put(manager+0x10,0);
    put(manager+0x27c,0); /* destroyed/replaced mover */
    CHECK(!nfsu1_player_camera(read_memory,0,&player));put(manager+0x27c,mover);
    put(mover,0x1f8e00); /* cinematic/replay mover */
    CHECK(!nfsu1_player_camera(read_memory,0,&player));put(mover,NFSU1_CAMERA_MOVER_VTABLE);
    put(mover+0x1c,camera+0x300); /* unrelated camera */
    CHECK(!nfsu1_player_camera(read_memory,0,&player));put(mover+0x1c,camera);
    put(NFSU1_VIEW(1)+0x44,NFSU1_VIEW(1)+0x44); /* empty main list */
    CHECK(!nfsu1_player_camera(read_memory,0,&player));
    put(NFSU1_VIEW(1)+0x44,0xffffffff);
    CHECK(!nfsu1_player_camera(read_memory,0,&player));
    put(NFSU1_VIEW(1)+0x44,mover+4);
    put(NFSU1_FLOW,3);CHECK(!nfsu1_player_camera(read_memory,0,&player));
    put(NFSU1_FLOW,4);CHECK(!nfsu1_player_camera(read_memory,0,&player));
    put(NFSU1_FLOW,6);CHECK(nfsu1_player_camera(read_memory,0,&player));

    /* The old camera read +60. Keep it deliberately different from the
     * render origin, and exercise the alternate pose used by retail 12C420. */
    const uint32_t car=0x23000;
    float rotation[12],position[3];
    const float ordinary_rotation[12]={1,0,0,0,0,1,0,0,0,0,1,0};
    const float alternate_rotation[12]={0,1,0,0,-1,0,0,0,0,0,1,0};
    const float ordinary_position[3]={80,120,3},wrong_position[3]={80,120,4.2f};
    const float alternate_position[3]={81,122,3.1f};
    memcpy(memory+car+0x50,ordinary_position,12);
    memcpy(memory+car+0x60,wrong_position,12);
    memcpy(memory+car+0x90,ordinary_rotation,48);
    memcpy(memory+car+0x4e0,alternate_rotation,48);
    memcpy(memory+car+0x520,alternate_position,12);
    put(car+0x530,0);
    CHECK(nfsu1_render_pose(read_memory,0,car,1,rotation,position));
    CHECK(!memcmp(position,ordinary_position,12) && !memcmp(rotation,ordinary_rotation,48));
    put(car+0x530,1u<<1);
    CHECK(nfsu1_render_pose(read_memory,0,car,1,rotation,position));
    CHECK(!memcmp(position,alternate_position,12) && !memcmp(rotation,alternate_rotation,48));
    CHECK(nfsu1_render_pose(read_memory,0,car,4,rotation,position));
    CHECK(!memcmp(position,ordinary_position,12) && !memcmp(rotation,ordinary_rotation,48));
    CHECK(!nfsu1_render_pose(read_memory,0,car,32,rotation,position));
    CHECK(!nfsu1_render_pose(read_memory,0,0xfffffff0,1,rotation,position));
    CHECK(!nfsu1_render_pose(read_memory,0,0x3ffff00,1,rotation,position));
    CHECK(!nfsu1_render_pose(read_memory,0,0x310000,1,rotation,position));

    /* Real-format synthetic XBE certificate. Only the owned in-memory copy
     * changes; wrong titles, truncation and wrapping offsets are rejected. */
    unsigned char original[1024]={0},copy[1024];uint32_t value;
    memcpy(original,"XBEH",4);
#define FIELD(offset,v) do { value=(v);memcpy(original+(offset),&value,4); } while(0)
    FIELD(0x104,0x10000);FIELD(0x118,0x10200);FIELD(0x200,0x1dc);
    FIELD(0x208,0x45410047);FIELD(0x29c,0x00ffffff);FIELD(0x3d8,0x12345679);
    memcpy(copy,original,sizeof copy);
    CHECK(nfsu1_host_image(copy,sizeof copy));
    memcpy(&value,copy+0x29c,4);CHECK(value==1);
    memcpy(&value,copy+0x3d8,4);CHECK(value==0x12345678);
    memcpy(copy+0x29c,original+0x29c,4);memcpy(copy+0x3d8,original+0x3d8,4);
    CHECK(!memcmp(copy,original,sizeof copy));
    CHECK(!nfsu1_host_image(copy,0x3db));
    FIELD(0x208,0x45410066);memcpy(copy,original,sizeof copy);
    CHECK(!nfsu1_host_image(copy,sizeof copy) && !memcmp(copy,original,sizeof copy));
    FIELD(0x118,0xfffffff0);memcpy(copy,original,sizeof copy);
    CHECK(!nfsu1_host_image(copy,sizeof copy));
    printf("NFSU1 title: %u UI, camera lifecycle, render-pose, memory-boundary and host-image checks passed.\n",checks);
    return 0;
}
