/* Included by quest_cockpit_test.c after its bounded page reader. Optional
 * private fixture: retail memory pages followed by labelled draw records. */
#include "../quest/runtime/quest_cockpit_windows.h"
static int cockpit_windows_test(const char *fixture)
{
    UiTestPage pages[2]={{.address=0x80010000},{.address=0x80020000}};
    UiTestMemory memory={pages,2};
    uint32_t info=0x80020000,texture=0x80020200,pixels=0x82010000;
    uint32_t entries[6][4]={
        {0x7b220ddf,0x12345678,0x80020200},
        {0x1b0763a0,0x12345678,0x80020200},
        {0xe7e4ef49,0x12345678,0x80020200},
        {0x4cdebfca,0x12345678,0x80020200},
        {0x60f8b13c,0x12345678,0x80020200},
        {0x0ab88f5d,0x12345678,0x80020200}};
    memcpy(pages[0].bytes+0x4d4,&info,4);
    memcpy(pages[1].bytes+0x79c,entries,sizeof entries);
    memcpy(pages[1].bytes+0x224,&entries[0][1],4);
    memcpy(pages[1].bytes+0x26c,&texture,4);
    memcpy(pages[1].bytes+0x270,&pixels,4);
    uint32_t windows[6],r[0x2000/4]={0};
    quest_cockpit_window_textures(ui_test_read,&memory,0x80010000,windows);
    for(int i=0;i<6;++i)CHECK(windows[i]==pixels);
    r[0x30c/4]=r[0x304/4]=1;r[0x344/4]=0x302;r[0x348/4]=0x303;r[0x1b0c/4]=0x40000000;
    CHECK(quest_cockpit_window_draw(windows,pixels,r,1));
    CHECK(!quest_cockpit_window_draw(windows,pixels,r,0)); /* exterior/other car/reflection */
    CHECK(!quest_cockpit_window_draw(windows,pixels+128,r,1)); /* unrelated alpha material */
    CHECK(!quest_cockpit_window_draw(windows,0,r,1));
    const uint32_t changed[][2]={{0x30c,0},{0x35c,1},{0x304,0},{0x344,1},{0x348,1},{0x1b0c,0}};
    for(unsigned i=0;i<sizeof changed/sizeof changed[0];++i) {
        uint32_t at=changed[i][0]/4,saved=r[at];r[at]=changed[i][1];
        CHECK(!quest_cockpit_window_draw(windows,pixels,r,1));r[at]=saved;
    }
    /* Texture reload, then stale TextureInfo, missing semantic and invalid
     * pointer: no old address may keep suppressing a new unrelated texture. */
    uint32_t relocated=pixels+4096;
    memcpy(pages[1].bytes+0x270,&relocated,4);
    quest_cockpit_window_textures(ui_test_read,&memory,0x80010000,windows);
    CHECK(!quest_cockpit_window_draw(windows,pixels,r,1));
    CHECK(quest_cockpit_window_draw(windows,relocated,r,1));
    const unsigned corrupt[]={0x224,0x26c,0x270};
    for(unsigned i=0;i<3;++i) {
        uint32_t saved,bad=0xffffffff;memcpy(&saved,pages[1].bytes+corrupt[i],4);
        memcpy(pages[1].bytes+corrupt[i],&bad,4);
        quest_cockpit_window_textures(ui_test_read,&memory,0x80010000,windows);
        for(int k=0;k<6;++k)CHECK(!windows[k]);
        memcpy(pages[1].bytes+corrupt[i],&saved,4);
    }
    memset(pages[1].bytes+0x79c,0,4);
    quest_cockpit_window_textures(ui_test_read,&memory,0x80010000,windows);
    CHECK(!windows[0] && windows[1]==relocated);
    memory.count=1;
    quest_cockpit_window_textures(ui_test_read,&memory,0x80010000,windows);
    for(int i=0;i<6;++i)CHECK(!windows[i]);
    quest_cockpit_window_textures(ui_test_read,&memory,0xfffffff0,windows);
    for(int i=0;i<6;++i)CHECK(!windows[i]);
    (void)fixture; /* NFSU2 captures have different ownership offsets. */
    puts("Cockpit windows: tint replacement, reload, invalid memory and draw-scope checks passed.");
    return 0;
}
