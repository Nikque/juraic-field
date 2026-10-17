"""Independent grid simulator, with no imports from the C++ implementation.

Boards of version 1 are 6 x 13 (12 rows that clear), boards of version 2 give
their size on the second line (10 x 19: 18 rows that clear).
"""
import pathlib,sys
root=pathlib.Path(sys.argv[1])
def read(path):
    lines=path.read_text().splitlines()
    if lines[0]=='JURAIC_FIELD_BOARD 1':rows=lines[1:]
    else:
        assert lines[0]=='JURAIC_FIELD_BOARD 2' and lines[1]=='10 19'
        rows=lines[2:]
    return [list(map(int,row.split())) for row in rows][::-1]
def fall(b):
    width,height=len(b[0]),len(b)
    for x in range(width):
        # Each fixed wall creates a new interval. Compact within each interval.
        barriers=[-1]+[y for y in range(height) if b[y][x]==10]+[height]
        for low,high in zip(barriers,barriers[1:]):
            values=[b[y][x] for y in range(low+1,high) if b[y][x]]
            for y in range(low+1,high):b[y][x]=values[y-low-1] if y-low-1<len(values) else 0
def simulate(b):
    b=[r[:] for r in b];fall(b);score=chain=0
    width,top=len(b[0]),len(b)-1  # the rows that clear: all but the hidden top row
    dirs=((1,0),(-1,0),(0,1),(0,-1))
    while True:
        seen=set();groups=[]
        for y in range(top):
            for x in range(width):
                if (x,y) in seen or not 1<=b[y][x]<=5:continue
                group={(x,y)};pending=[(x,y)];seen.add((x,y))
                while pending:
                    px,py=pending.pop()
                    for dx,dy in dirs:
                        nx,ny=px+dx,py+dy
                        if 0<=nx<width and 0<=ny<top and (nx,ny) not in seen and b[ny][nx]==b[y][x]:
                            seen.add((nx,ny));group.add((nx,ny));pending.append((nx,ny))
                if len(group)>=4:groups.append(group)
        if not groups:break
        chain+=1;colored=set.union(*groups);erase=colored.copy();colors={b[y][x] for x,y in colored}
        base=len(colored)*10;point_bonus=0
        for y in range(top):
            for x in range(width):
                damage=sum((x+dx,y+dy) in colored for dx,dy in dirs)
                if not damage:continue
                kind=b[y][x]
                if kind in (6,7):erase.add((x,y));point_bonus+=50 if kind==7 else 0
                elif kind==8:
                    if damage>1:erase.add((x,y));base+=60
                    else:b[y][x]=6;base+=10
        group_bonus=sum(10 if len(g)>=11 else len(g)-3 if len(g)>=5 else 0 for g in groups)
        # (chains past 19 use the bonus of the 19th)
        chain_bonus=([0,8,16,32]+list(range(64,513,32)))[min(chain,19)-1]
        score+=base*max(1,chain_bonus+[0,0,3,6,12,24][len(colors)]+group_bonus)+point_bonus
        for x,y in erase:b[y][x]=0
        fall(b)
    return b,chain,score
for prefix,count in (('',500),('wide-',300)):
    for i in range(count):
        final,chain,score=simulate(read(root/f'{prefix}{i}.jfboard'))
        assert final==read(root/f'{prefix}{i}.final'),(prefix,i)
        assert [chain,score]==list(map(int,(root/f'{prefix}{i}.stats').read_text().split())),(prefix,i)
print('PASS independent simulation of 500 boards of 6 x 13 and 300 of 10 x 19: specials, walls, the hidden row, chains and scores')
