import json, sys, numpy as np
from PIL import Image
from collections import defaultdict
meta=json.load(open('meta.json'))
n=len(meta); w=len(str(n))
def dil(a):
    b=a.copy(); b[1:]|=a[:-1]; b[:-1]|=a[1:]; c=b.copy(); c[:,1:]|=b[:,:-1]; c[:,:-1]|=b[:,1:]; return c
masks=[np.array(Image.open(f'e-{str(i+1).zfill(w)}.pgm'))<200 for i in range(n)]
bypage=defaultdict(list)
for i,m in enumerate(meta): bypage[m['page']].append(i)
cnt=0
for pg,idx in sorted(bypage.items()):
    for a in idx:
        if meta[a]['kind']!='text': continue
        da=dil(masks[a])
        for b in idx:
            if b==a or (meta[b]['kind']=='text' and b<a): continue
            ov=int((da&masks[b]).sum())
            if ov>0:
                cnt+=1; print(pg, ov, repr(meta[a]['label']), '<->', repr(meta[b]['label']))
print('total',cnt)
