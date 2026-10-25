#include "interaction.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Failed line %d: %s (%s)\n",__LINE__,#x,c.message);return 1;}}while(0)
int main(int argc,char **argv){
    McClient c;mc_init(&c);CHECK(argc>3 && net_init()==0);
    CHECK(mc_select_slot(&c,1)==-1 && mc_creative_slot(&c,0,1)==-1);
    CHECK(mc_login(&c,argv[2],atoi(argv[3]),"VitaPlayer")==0);
    uint64_t end=net_millis()+2000;int sent=0,corrected=0;
    while(net_millis()<end){
        CHECK(mc_step(&c)>=0);
        if(c.position_ready && !sent){
            CHECK(c.gamemode==1 && c.hotbar[0].item==23 && c.hotbar[0].count==12);
            CHECK(c.hotbar[1].item==165 && c.hotbar[1].count==5);
            CHECK(mc_select_slot(&c,9)==-1 && mc_select_slot(&c,1)==0);
            CHECK(mc_creative_slot(&c,1,261)==0);
            float cursor[3]={.25f,1,.75f};
            CHECK(mc_block_action(&c,0,-4,-2,-7,1,cursor)==0);
            CHECK(mc_block_action(&c,1,-4,-2,-7,1,cursor)==0);
            CHECK(c.interaction_sequence==2);sent=1;
        }
        if(sent && c.interaction_ack==2 && c.gamemode==0){
            CHECK(c.selected_slot==4 && c.hotbar[1].item==-1 && c.hotbar[1].count==0);
            CHECK(mc_creative_slot(&c,0,1)==-1);
            float cursor[3]={0,0,0};CHECK(mc_block_action(&c,0,0,0,0,0,cursor)==-1);
            corrected=1;break;
        }
    }
    CHECK(sent && corrected && c.break_packets==1 && c.place_packets==1);
    mc_close(&c);net_term();puts("interactions=2");return 0;
}
