import struct,sys
# FEL detector: the letterbox box in the EL draws horizontal edges near y=68 and y=470 (960x540 capture).
def load(f):
    d=open(f,'rb').read(); o=struct.unpack_from('<I',d,10)[0]; w,h=struct.unpack_from('<ii',d,18); b=struct.unpack_from('<H',d,28)[0]//8; st=(w*b+3)&~3
    def px(x,y):
        yy=(abs(h)-1-y) if h>0 else y; p=o+yy*st+x*b; return (d[p]+d[p+1]+d[p+2])/3
    return px,w,abs(h)
def edge(f):
    px,w,h=load(f); best=0
    for y0 in list(range(55,85))+list(range(455,485)):
        s=sum(abs(px(x,y0+1)-px(x,y0-1)) for x in range(300,940,4))/160; best=max(best,s)
    return best
for f in sys.argv[1:]: print('%-28s edge=%.1f'%(f.split('/')[-1],edge(f)))
