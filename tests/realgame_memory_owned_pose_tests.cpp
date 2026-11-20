#include "../play/memory/owned_pose14.h"
#include <cstdio>
using namespace play::realgame;
struct Fixture {
    FieldContext context;uint64_t player=0x10000,operate=0x20000,field=0x30000;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x40000);
    int reads=0,poses=0;bool torn=false,tornOperator=false,readInput=false;
    template<class T>void put(uint64_t at,T value){std::memcpy(ram.data()+at,&value,sizeof value);}
    Fixture(){
        context.process={1,10,0x140000000};auto& h=context.process.image;
        h.ok=true;h.machine=0x8664;h.timeStamp=0x66a8c684;h.imageSize=0x6fd000;
        put(player,context.process.imageBase+0x2ac500);put(player+64,operate);put(player+152,operate);put(player+128,field);
        put(operate,context.process.imageBase+0x2ad2c0);put(operate+40,player);put(operate+48,field);
        put(field,context.process.imageBase+0x2b1470);put(field+80,player);
        put(player+0x50,uint32_t(1));put(player+0x5e,uint8_t(0));
        put(operate+0xcc,4.f);put(operate+0xd0,5.f);put(operate+0xdc,4.f);put(operate+0xe0,4.5f);
        put(operate+0xe8,uint32_t(1));put(operate+0xec,uint8_t(5));put(operate+0xed,uint8_t(8));
        put(operate+0x9d,uint8_t(1));put(operate+0xa0,uint32_t(19));put(operate+0xa8,uint32_t(2));put(operate+0xac,uint32_t(5));
        put(field+0x108,uint32_t(500));put(field+0x10c,uint32_t(20));put(field+0x2cc,uint32_t(0));put(field+0x2d0,uint32_t(1));
    }
    FieldRead14 read=[this](uint64_t a,void* out,size_t n){
        ++reads;readInput=readInput || (a<operate+0x9c && a+n>operate+0x98);
        if(a>ram.size() || n>ram.size()-size_t(a))return false;
        if(a==operate+0xcc && ++poses==2 && torn)put(field+0x108,uint32_t(501));
        if(a==operate+0xcc && poses==1 && tornOperator)put(player+152,operate+4);
        std::memcpy(out,ram.data()+a,n);return true;
    };
    auto sample(){return readOwnedPublicPose14(context,player,read);}
};
int main(){int checks=0,failed=0;const auto check=[&](bool b,const char* s){++checks;if(!b){++failed;std::printf("FAIL %s\n",s);}};
    {Fixture f;const auto p=f.sample();check(p && p->seat==0 && p->x==4 && p->y==6 && p->upper && p->dir==1,"typed current pose and half cell decoded");
     check(p && p->operating && p->lockedPairs==19 && p->count108==500 && p->currentColors==std::array<int,2>{2,5},"current control flag, lock index, update counter and colors");
     check(p && p->owner.context.discoveryOnly,"structural data is never an input authorization");
     check(!f.readInput,"input word is never copied or decoded");}
    {Fixture f;f.put(f.player+0x5e,uint8_t(1));check(f.sample()->seat==1,"second seat decoded from the player's own identity");}
    {Fixture f;f.put(f.player+64,uint64_t(0));check(bool(f.sample()),"stale base operator cannot replace current operator");}
    {Fixture f;f.put(f.player+152,uint64_t(0));check(!f.sample(),"missing current operator must not fall back to base copy");}
    {Fixture f;f.tornOperator=true;check(!f.sample(),"current operator replacement between passes is rejected");}
    {Fixture f;f.put(f.operate+0xcc,3.5f);f.put(f.operate+0xdc,3.5f);const auto p=f.sample();
     check(p && p->x==4 && p->horizontalMoving && p->operating,"rightward half-column keeps collision column and movement state");}
    {Fixture f;f.put(f.operate+0xcc,4.5f);f.put(f.operate+0xdc,4.5f);const auto p=f.sample();
     check(p && p->x==4 && p->horizontalMoving,"leftward half-column must not round to its previous column");}
    {Fixture f;f.put(f.operate+0xcc,4.25f);f.put(f.operate+0xdc,4.25f);check(!f.sample(),"unprofiled horizontal fraction is rejected");}
    {Fixture f;f.put(f.operate+0x9d,uint8_t(0));f.put(f.operate+0xa0,uint32_t(20));const auto p=f.sample();check(p && !p->operating && p->lockedPairs==20,"locked pose remains data, not control permission");}
    {Fixture f;f.torn=true;check(!f.sample(),"update between two passes refuses the entire sample");}
    {Fixture f;f.put(f.operate,uint64_t(0));check(!f.sample(),"destroyed operator rejected");}
    {Fixture f;f.put(f.field+80,f.player+4);check(!f.sample(),"foreign field backlink rejected");}
    {Fixture f;f.put(f.player+128,f.field+4);check(!f.sample(),"foreign field type rejected");}
    {Fixture f;f.put(f.player+0x5e,uint8_t(2));check(!f.sample(),"invalid seat rejected");}
    {Fixture f;f.put(f.operate+0xe8,uint32_t(4));check(!f.sample(),"invalid orientation rejected");}
    {Fixture f;f.put(f.operate+0x9d,uint8_t(2));check(!f.sample(),"nonboolean active flag rejected");}
    {Fixture f;f.put(f.operate+0xe0,4.25f);check(!f.sample(),"unexpected half-cell representation rejected");}
    {Fixture f;f.put(f.operate+0xec,uint8_t(3));check(!f.sample(),"integer and floating columns must agree");}
    {Fixture f;f.put(f.operate+0xa8,uint32_t(0));check(!f.sample(),"unknown current color cannot authorize a falling piece");}
    {Fixture f;f.context.process.image.timeStamp++;check(!f.sample() && !f.reads,"unsupported version rejected before any read");}
    {Fixture f;f.context.process.creationTime=0;check(!f.sample() && !f.reads,"missing process lifetime rejected before reads");}
    {Fixture f;f.player=UINT64_MAX-10;check(!f.sample() && !f.reads,"overflowing object address rejected before reads");}
    {Fixture f;f.context.process.imageBase=UINT64_MAX-1;check(!f.sample() && !f.reads,"overflowing image rejected before reads");}
    std::printf("owned public pose: %d checks, %d failures\n",checks,failed);return failed?1:0;
}
