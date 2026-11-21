import struct,sys,numpy as np
# FEL detector for display-led (Dolby Vision tunnel) captures: the tunnel's colour noise hides
# per-pixel edges, but the EL letterbox box still shifts whole rows near y=68 and y=470 (960x540).
def load(f):
    d=open(f,'rb').read(); o=struct.unpack_from('<I',d,10)[0]; w,h=struct.unpack_from('<ii',d,18); b=struct.unpack_from('<H',d,28)[0]//8
    st=(w*b+3)&~3; a=np.frombuffer(d,np.uint8,count=st*abs(h),offset=o).reshape(abs(h),st)[:,:w*b].reshape(abs(h),w,b)[:,:,:3]
    return (a[::-1] if h>0 else a).astype(float)
def score(f):
    a=load(f); r=a[:,300:940].mean(axis=(1,2)); best=0
    for y in list(range(55,85))+list(range(455,485)):
        best=max(best,abs(r[y+2:y+6].mean()-r[y-5:y-1].mean()))
    return best
for f in sys.argv[1:]: print('%-40s step=%.1f'%('/'.join(f.split('/')[-2:]),score(f)))
