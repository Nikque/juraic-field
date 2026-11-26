#define main owned_legacy_main
#include "realgame_memory_owned_pose_tests.cpp"
#undef main
#include "../play/memory/published_next14.h"
#include <iostream>
static int checks=0,failures=0;
static void check(bool b,const char*s){++checks;if(!b){++failures;std::cout<<"FAIL "<<s<<'\n';}}
struct NextFixture:Fixture{
 MemoryMotionSample14 packet;Tracker t;int seat=0;uint64_t root=0x34000;int payloadReads=0;bool forbidden=false,tornNext=false,changedCounter=false;int firstReads=0;
 NextFixture(int side=0):seat(side){
  context.round=2;context.roundKnown=true;put(player+0x5e,uint8_t(seat));put(field+0x11c,uint8_t(0));put(field+0x18d,uint8_t(0));
  packet.context=context;packet.generation=3;packet.observedMs=1670;packet.frame=100;packet.roundStart=10;packet.poseFrame[seat]=100;
  packet.bound[seat]=packet.running[seat]=packet.advancing[seat]=true;packet.pairs[seat]=20;packet.pose[seat]=sample();
  t.fromStart=true;t.roundStart=10;t.frame=100;auto& s=t.side[seat];s.pairs=20;s.colors={2,5};s.clear=s.inHand=s.pair.seen=true;
  auto& image=t.last.side[seat];image.nextRead[0]=image.nextRead[1]=true;image.next[0]={1,3};image.next[1]={4,2};
  put(player+0x90,root+0x20);put(root,context.process.imageBase+0x2ac288);put(root+0x20,context.process.imageBase+0x2ac300);put(root+0x28,player);
  for(auto at:{0x88,0xc0,0xe8})put(root+at,uint32_t(0x201));
  put(root+0x8c,uint32_t(2));put(root+0x90,uint32_t(5));put(root+0xc4,uint32_t(1));put(root+0xc8,uint32_t(3));put(root+0xec,uint32_t(4));put(root+0xf0,uint32_t(2));
 }
 FieldRead14 bounded=[this](uint64_t a,void*out,size_t n){
  if(a>=root&&a<root+0x160){
   const bool allowed=(a==root&&n==8)||(a==root+0x20&&n==8)||(a==root+0x28&&n==8)||(a==root+0x38&&n==12)||((a==root+0x88||a==root+0xc0||a==root+0xe8)&&n==12);
   if(!allowed){forbidden=true;return false;}
   if(a>=root+0x88){++payloadReads;if(a==root+0xc0&&++firstReads==2&&tornNext)put(root+0xc4,uint32_t(2));}
   if(a==root+0x38&&changedCounter)put(field+0x108,uint32_t(501));
  }
  return read(a,out,n);
 };
 auto get(uint64_t now=1670){return readPublishedNext14(packet,t,seat,now,1,bounded);}
};
int readerTests(){
 for(int seat=0;seat<2;++seat){
  {NextFixture f(seat);auto x=f.get();check(x&&x->next[0].axis==1&&x->next[0].child==3&&x->next[1].axis==4&&x->next[1].child==2,"image-published two slots verify for each seat");check(f.payloadReads==6&&!f.forbidden&&!f.readInput,"only exact public records, no third/ring/RNG/input read");}
  {NextFixture f(seat);f.t.last.side[seat].nextRead[1]=false;int before=f.reads;check(!f.get()&&f.reads==before,"unread public NEXT rejects before any game read");}
  {NextFixture f(seat);f.t.frame=96;int before=f.reads;check(!f.get()&&f.reads==before,"old image rejects before game reads");}
  {NextFixture f(seat);f.packet.advancing[seat]=false;int before=f.reads;check(!f.get()&&f.reads==before,"stopped native stream rejects before game reads");}
  {NextFixture f(seat);f.t.side[seat].unseen=1;int before=f.reads;check(!f.get()&&f.reads==before,"unresolved serial rejects before game reads");}
  {NextFixture f(seat);f.packet.generation=0;check(!f.get()&&!f.payloadReads,"missing generation cannot expose records");}
  {NextFixture f(seat);f.packet.context.discoveryOnly=true;check(!f.get()&&!f.payloadReads,"structural discovery is not display proof");}
  {NextFixture f(seat);f.packet.context.process.creationTime++;check(!f.get()&&!f.payloadReads,"changed process lifetime rejected");}
  {NextFixture f(seat);f.t.roundStart++;check(!f.get()&&!f.payloadReads,"round mismatch rejected");}
  {NextFixture f(seat);f.put(f.root+0x38,uint32_t(1));check(!f.get()&&!f.payloadReads,"slide rejects before reading any slot");}
  {NextFixture f(seat);f.put(f.root+0x40,0.5f);check(!f.get()&&!f.payloadReads,"nonzero interpolation rejects before payload");}
  {NextFixture f(seat);f.put(f.root,uint64_t(f.context.process.imageBase+0x2ac370));check(!f.get()&&!f.payloadReads,"unverified lesson type refuses payload");}
  {NextFixture f(seat);f.put(f.root+0x28,f.player+8);check(!f.get()&&!f.payloadReads,"foreign Next owner refuses payload");}
  {NextFixture f(seat);f.put(f.root+0xc0,uint32_t(0x300));check(!f.get()&&!f.forbidden,"unverified larger shape rejected without wider read");}
  {NextFixture f(seat);f.put(f.root+0xc4,uint32_t(6));check(!f.get(),"unsupported colour rejected");}
  {NextFixture f(seat);f.put(f.root+0x8c,uint32_t(3));check(!f.get(),"current descriptor must match public operator");}
  {NextFixture f(seat);f.t.last.side[seat].next[0]={3,1};check(!f.get(),"axis-child source mismatch rejected, no cached value");}
  {NextFixture f(seat);f.tornNext=true;check(!f.get(),"torn pair bytes rejected by double read");}
  {NextFixture f(seat);f.changedCounter=true;check(!f.get(),"current update changed across read rejects sample");}
 }
 std::cout<<"published NEXT verifier "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}


#include "../play/realgame_follow.h"
#include "../play/memory/sample14.h"
#include <sstream>
static void observationAuditTest(){
 using namespace juraic::inputdiag;
 NextFixture f(0);auto n=f.get();Follower ff;ff.tracker=f.t;
 ff.tracker.pairsOf[0].assign(20,juraic::Pair{2,5});ff.tracker.knownOf[0].assign(20,1);
 juraic::Player own;own.phase=juraic::Phase::Control;own.nextPair=20;own.moves=19;own.colors={2,5};
 juraic::ai::Observation o;
 check(bool(n)&&ff.useMemoryNext(*n,f.packet,0,1670,1),"audit setup accepts packet");
 check(trace.open("next-audit-test.tsv",nullptr),"audit trace opens");
 ff.observeAt(0,ff.rules,own,100,o);
 check(o.tsumo.pairs.size()==22&&o.tsumo.pairs[20].axis==1&&o.tsumo.pairs[21].child==2,"active audit preserves observed NEXT values");
 ff.observeAt(0,ff.rules,own,100,o);
 ff.tracker.frame=104;ff.observeAt(0,ff.rules,own,104,o);
 ff.tracker.frame=100;ff.tracker.knownOf[0][19]=0;ff.observeAt(0,ff.rules,own,100,o);
 ff.tracker.knownOf[0][19]=1;ff.observeAt(0,ff.rules,own,100,o);
 ff.clearMemoryFields();ff.noteNextUnavailable(0);ff.observeAt(0,ff.rules,own,100,o);
 check(o.tsumo.pairs.size()==20,"source loss restores image while trace active");
 check(ff.useMemoryNext(*n,f.packet,0,1670,1),"same pair can regain verified NEXT");
 ff.observeAt(0,ff.rules,own,100,o);
 trace.close();
 std::ifstream in("next-audit-test.tsv");std::string line;std::vector<int> statuses;
 while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;
  std::istringstream s(line);int64_t timestamp,sequence,kind,seat,round,pair,frame,status;
  if(s>>timestamp>>sequence>>kind>>seat>>round>>pair>>frame>>status && kind==NEXT_OBSERVATION)statuses.push_back(int(status));
 }
 check(statuses==std::vector<int>({1,2,3,1,1}),"observation trace distinguishes stale, unknown prefix and same-pair reacquisition");
 check(trace.open("next-audit-test-reopen.tsv",nullptr),"audit trace reopens");
 ff.observeAt(0,ff.rules,own,100,o);trace.close();
 std::ifstream reopened("next-audit-test-reopen.tsv");int records=0;
 while(std::getline(reopened,line))if(!line.empty()&&line[0]!='#')++records;
 check(records==1,"new trace session records unchanged source afresh");
}
static void readerStageAuditTest(){
 {NextFixture f;PublishedNextAudit14 a;auto n=readPublishedNext14(f.packet,f.t,0,1670,1,f.bounded,&a);
  check(bool(n)&&a.stage==11,"reader audit records verification success");}
 {NextFixture f;f.packet.advancing[0]=false;PublishedNextAudit14 a;auto n=readPublishedNext14(f.packet,f.t,0,1670,1,f.bounded,&a);
  check(!n&&a.stage==1&&f.payloadReads==0,"reader audit records pre-read pose refusal");}
 {NextFixture f;f.t.last.side[0].nextRead[1]=false;PublishedNextAudit14 a;auto n=readPublishedNext14(f.packet,f.t,0,1670,1,f.bounded,&a);
  check(!n&&a.stage==2&&f.payloadReads==0,"reader audit records missing public image");}
 {NextFixture f;f.put(f.root+0xc0,uint32_t(0x202));PublishedNextAudit14 a;auto n=readPublishedNext14(f.packet,f.t,0,1670,1,f.bounded,&a);
  check(!n&&a.stage==8,"reader audit records unsupported kind without accepting it");}
 {NextFixture f;f.t.last.side[0].next[0]={3,1};PublishedNextAudit14 a;auto n=readPublishedNext14(f.packet,f.t,0,1670,1,f.bounded,&a);
  check(!n&&a.stage==10,"reader audit records image correspondence failure");}
}
static bool visible(const juraic::ai::Observation& o,int seat){
 const auto& q=seat==0?o.tsumo:o.tsumo1;
 return q.pairs.size()>=22 && q.pairs[20].axis==1 && q.pairs[20].child==3 && q.pairs[21].axis==4 && q.pairs[21].child==2;
}
static void imageRefreshTest(){
 for(int seat=0;seat<2;++seat){
  NextFixture f(seat);auto n=f.get();check(bool(n),"reader validates initial image and native records");if(!n)continue;
  Follower ff;ff.tracker=f.t;ff.tracker.pairsOf[seat].assign(20,juraic::Pair{2,5});ff.tracker.knownOf[seat].assign(20,1);
  ff.tracker.side[1-seat].clear=true;
  MemorySample14 packet;packet.value.context=f.context;packet.value.observedMs=1670;packet.frame=100;packet.roundStart=10;
  for(int side=0;side<2;++side){packet.pairs[side]=ff.tracker.side[side].pairs;packet.unseen[side]=ff.tracker.side[side].unseen;packet.value.fields[side]=ff.tracker.side[side].resting;}
  packet.next[seat]=n;
  check(usableMemorySample14(packet,ff.tracker,1670,1),"board packet passed consumer gate");
  check(ff.useMemoryFields(packet.value.fields),"actual Follower accepts board fields");
  juraic::Player own;own.phase=juraic::Phase::Control;own.nextPair=20;own.moves=19;own.colors={2,5};juraic::ai::Observation o;
  check(ff.useMemoryNext(*n,f.packet,seat,1670,1),"same image consumer accepts");ff.observeAt(seat,ff.rules,own,100,o);check(visible(o,seat),"same image reaches actual Follower observation");
  ff.tracker.frame=101;check(ff.useMemoryNext(*n,f.packet,seat,1680,1),"newer matching image remains usable");
  ff.observeAt(seat,ff.rules,own,101,o);check(visible(o,seat),"matching newer image retains observed next");
  ff.tracker.last.side[seat].nextRead[1]=false;
  ff.observeAt(seat,ff.rules,own,101,o);check(!visible(o,seat),"Follower rejects retained packet after newer unread image");
  check(!ff.useMemoryNext(*n,f.packet,seat,1680,1),"consumer rejects unread newer image");
  ff.tracker.last.side[seat].nextRead[1]=true;ff.tracker.last.side[seat].next[0]={3,1};
  check(!ff.useMemoryNext(*n,f.packet,seat,1680,1),"consumer rejects changed axis-child newer image");
  ff.tracker.last.side[seat].next[0]={1,3};ff.tracker.last.side[seat].next[1]={4,3};
  check(!ff.useMemoryNext(*n,f.packet,seat,1680,1),"consumer rejects changed second slot");
  ff.tracker.last.side[seat].next[1]={4,2};check(ff.useMemoryNext(*n,f.packet,seat,1680,1),"same packet can be reaccepted after matching image returns");
  ff.observeAt(seat,ff.rules,own,101,o);check(visible(o,seat),"reaccepted packet reaches Follower");
  check(!ff.useMemoryNext(*n,f.packet,seat,1721,1),"older than 50 ms rejects");
  ++ff.tracker.side[seat].pairs;check(!ff.useMemoryNext(*n,f.packet,seat,1680,1),"next pair rejects previous packet");
  --ff.tracker.side[seat].pairs;
  const std::string path="next-image-trace-"+std::to_string(seat)+".tsv";
  check(juraic::inputdiag::trace.open(path,nullptr),"trace opens");
  check(ff.useMemoryNext(*n,f.packet,seat,1680,1),"trace setup accepted");ff.observeAt(seat,ff.rules,own,101,o);
  ff.tracker.last.side[seat].nextRead[0]=false;ff.observeAt(seat,ff.rules,own,101,o);
  ff.clearMemoryFields();ff.noteNextUnavailable(seat);ff.tracker.last.side[seat].nextRead[0]=true;
  check(ff.useMemoryNext(*n,f.packet,seat,1680,1),"trace same-packet reacquisition accepted");ff.observeAt(seat,ff.rules,own,101,o);
  juraic::inputdiag::trace.close();
  std::ifstream in(path);std::string line;std::vector<int> statuses;
  while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;std::istringstream s(line);int64_t ts,seq,kind,which,round,pair,frame,status;
   if(s>>ts>>seq>>kind>>which>>round>>pair>>frame>>status && kind==juraic::inputdiag::NEXT_OBSERVATION)statuses.push_back(int(status));}
  check(statuses==std::vector<int>({1,2,1}),"trace records image loss and same-packet reacquisition");
 }
}

int main(){
 readerTests();
 for(int seat=0;seat<2;++seat){
  {NextFixture f(seat);f.put(f.root+0xc0,uint32_t(0x200));check(!f.get(),"constructor-only kind 0x200 is not the verified live pair kind");}
  {NextFixture f(seat);f.put(f.root+0xc0,uint32_t(0x202));check(!f.get(),"unverified low kind bits are not masked into acceptance");}
  {NextFixture f(seat);auto n=f.get();check(n&&usablePublishedNext14(*n,f.packet,f.t,seat,1670,1),"fresh published sample reaches the consumer");
   auto old=*n;old.observedMs-=51;check(!usablePublishedNext14(old,f.packet,f.t,seat,1670,1),"expired NEXT falls back");
   old=*n;old.generation++;check(!usablePublishedNext14(old,f.packet,f.t,seat,1670,1),"old binding generation falls back");
   old=*n;old.roundStart++;check(!usablePublishedNext14(old,f.packet,f.t,seat,1670,1),"other round falls back");
   old=*n;old.player++;check(!usablePublishedNext14(old,f.packet,f.t,seat,1670,1),"other player falls back");
   check(!usablePublishedNext14(*n,f.packet,f.t,seat,1670,2),"different process falls back");
   Follower ff;ff.tracker=f.t;ff.tracker.pairsOf[seat].assign(20,juraic::Pair{2,5});ff.tracker.knownOf[seat].assign(20,1);
   check(ff.useMemoryNext(*n,f.packet,seat,1670,1),"Host or Guest accepts its own proved public NEXT");
   juraic::Player own;own.phase=juraic::Phase::Control;own.nextPair=20;own.moves=19;own.colors={2,5};
   juraic::ai::Observation o;ff.observeAt(seat,ff.rules,own,100,o);auto& q=seat==0?o.tsumo:o.tsumo1;
   check(q.pairs.size()==22&&q.pairs[20].axis==1&&q.pairs[20].child==3&&q.pairs[21].axis==4&&q.pairs[21].child==2,"actual Follower observation exposes exactly the two public slots to search");
   ff.clearMemoryFields();ff.observeAt(seat,ff.rules,own,100,o);auto& fallback=seat==0?o.tsumo:o.tsumo1;
   check(fallback.pairs.size()==20&&!ff.memoryNextAvailable(seat),"source loss clears NEXT and restores image observation");
   ff.tracker.knownOf[seat][19]=0;check(ff.useMemoryNext(*n,f.packet,seat,1670,1),"proof may exist while historical prefix is incomplete");
   ff.observeAt(seat,ff.rules,own,100,o);auto& gaps=seat==0?o.tsumo:o.tsumo1;
   check(gaps.pairs.size()<22,"unknown historical prefix is never invented to connect NEXT");
  }
 }
 observationAuditTest();
 readerStageAuditTest();
 imageRefreshTest();
 std::cout<<"published NEXT product "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;
}
