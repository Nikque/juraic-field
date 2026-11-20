#include "../play/memory/owner14.h"
#include <cstdio>
using namespace play::realgame;
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x50000);
    FieldContext context;uint64_t top=0x10000,operate=top+716,player=0x20000,field=0x30000;
    int reads=0,operatorReads=0;bool tear=false;
    void put(uint64_t a,uint64_t value){std::memcpy(ram.data()+a,&value,8);}
    void install(){
        put(operate,context.process.imageBase+0x2ad2c0);put(operate+40,player);put(operate+48,field);
        put(player,context.process.imageBase+0x2ac500);put(player+64,operate);put(player+128,field);
        put(field,context.process.imageBase+0x2b1470);put(field+80,player);
    }
    FieldRead14 read=[this](uint64_t a,void* into,size_t n){
        ++reads;if(a>ram.size() || n>ram.size()-size_t(a))return false;
        if(a==operate && ++operatorReads==2 && tear)put(operate,context.process.imageBase+0x1000);
        std::memcpy(into,ram.data()+a,n);return true;
    };
    Fixture(){
        context.process.pid=1;context.process.creationTime=99;context.process.imageBase=0x140000000;
        context.process.image.ok=true;context.process.image.machine=0x8664;context.process.image.timeStamp=0x66a8c684;context.process.image.imageSize=0x6fd000;
        context.round=7;context.roundKnown=true;
        for(int r=0;r<14;++r)for(int x=0;x<6;++x){const uint32_t value=uint32_t(9+r*8+x)<<16;std::memcpy(ram.data()+top-64+r*32+x*4,&value,4);}
        install();
    }
    auto probe(){return discoverOwnerCandidates14(context,top,read);}
};
int main(){
    int checks=0,failed=0;const auto check=[&](bool b,const char* s){++checks;if(!b){++failed;std::printf("FAIL %s\n",s);}};
    {Fixture f;auto r=f.probe();check(r.size()==1 && r[0].operate==f.operate && r[0].player==f.player && r[0].field==f.field,"typed reciprocal owner cycle");
     check(r.size()==1 && r[0].context.discoveryOnly && r[0].context.round==7 && r[0].context.process.creationTime==99,"owner evidence stays diagnostic and process scoped");}
    {Fixture f;f.put(f.operate,0);f.operate-=120;f.install();auto r=f.probe();check(r.size()==1 && r[0].operate==f.operate,"allocation displacement does not become a fixed field offset");}
    {Fixture f;f.put(f.operate,f.context.process.imageBase+0x1000);check(f.probe().empty(),"destroyed base-class object rejected even when grid remains valid");}
    {Fixture f;f.put(f.player,0);check(f.probe().empty(),"wrong player type rejected");}
    {Fixture f;f.put(f.field,0);check(f.probe().empty(),"wrong field type rejected");}
    {Fixture f;f.put(f.player+64,1);check(f.probe().empty(),"player must refer back to this operator");}
    {Fixture f;f.put(f.player+128,1);check(f.probe().empty(),"player must refer to this field");}
    {Fixture f;f.put(f.field+80,1);check(f.probe().empty(),"field must refer back to this player");}
    {Fixture f;f.tear=true;check(f.probe().empty(),"destruction between passes revokes candidate");}
    {Fixture f;f.context.process.image.timeStamp=0;check(f.probe().empty() && f.reads==0,"unsupported image rejected before reads");}
    {Fixture f;f.context.process.creationTime=0;check(f.probe().empty() && f.reads==0,"missing process identity rejected before reads");}
    {Fixture f;f.context.roundKnown=false;check(f.probe().empty() && f.reads==0,"unknown round rejected before reads");}
    {Fixture f;f.ram[f.top-64+2]^=1;check(f.probe().empty(),"invalid field signature rejected");}
    {Fixture f;f.top=UINT64_MAX-100;check(f.probe().empty() && f.reads==0,"overflowing discovery window rejected before reads");}
    {Fixture f;f.put(f.operate+40,f.context.process.imageBase+0x100);check(f.probe().empty() && f.reads==3,"image/code pointers never dereferenced as heap owners");}
    {Fixture f;f.context.process.imageBase=UINT64_MAX-1;check(f.probe().empty() && f.reads==0,"overflowing image range rejected before reads");}
    {Fixture f;const auto first=f.operate;f.operate+=512;f.player+=512;f.field+=512;f.install();const auto r=f.probe();
     check(r.size()==2 && r[0].operate==first && r[1].operate==f.operate,"multiple reciprocal cycles stay ambiguous; discovery never chooses a seat");}
    std::printf("memory owner discovery: %d checks, %d failures\n",checks,failed);return failed?1:0;
}
