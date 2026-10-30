/* SPDX-License-Identifier: MIT */
#include "stereo-declare.h"
#include <X11/Xatom.h>
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void set(Display *d, Window w, Atom a, unsigned long *v, int count)
{
    XChangeProperty(d, w, a, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)v, count);
}

int main(void)
{
    void *lib = dlopen("libstereo-declare.so.1", RTLD_NOW);
    assert(lib);
    if (!dlsym(lib, "stereo_x11_presentation_create")) {
        puts("old library has no displayed-receipt API");
        return 42;
    }
    Display *d = XOpenDisplay(NULL); assert(d);
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 64, 32, 0, 0, 0);
    Atom cap = XInternAtom(d, "_KDE_NET_WM_STEREO_PRESENTATION_SUPPORTED", False);
    Atom req = XInternAtom(d, "_KDE_NET_WM_STEREO_PRESENTATION_REQUEST", False);
    Atom res = XInternAtom(d, "_KDE_NET_WM_STEREO_PRESENTATION_RECEIPT", False);
    struct stereo_x11_presentation *p = NULL;
    struct stereo_x11_presentation_receipt receipt;
    uint32_t clock_id = 99;
    assert(stereo_x11_presentation_supported(d, w, &clock_id) == 0 && clock_id == 99);
    assert(stereo_x11_presentation_create(d, w, 5, &p) == -ENOTSUP && !p);
    Atom type; int format; unsigned long count, extra; unsigned char *data;
    XGetWindowProperty(d,w,req,0,12,False,AnyPropertyType,&type,&format,&count,&extra,&data);
    assert(type==None);XFree(data);
    unsigned long caps[9] = {1, 1, 1, 16, 12, 0, 0, 0, 0};
    for (int n=0;n<10;n++) {
        if(n==8)continue;
        set(d,DefaultRootWindow(d),cap,caps,n>8?9:n);
        assert(stereo_x11_presentation_supported(d,w,&clock_id)==-EPROTO);
    }
    for(unsigned i=0;i<8;i++) {
        if(i==1)continue; /* the compositor owns its clock value */
        caps[i]++;set(d,DefaultRootWindow(d),cap,caps,8);
        assert(stereo_x11_presentation_supported(d,w,&clock_id)==-EPROTO);caps[i]--;
    }
    XChangeProperty(d,DefaultRootWindow(d),cap,XA_STRING,8,PropModeReplace,(unsigned char*)caps,8);
    assert(stereo_x11_presentation_supported(d,w,&clock_id)==-EPROTO);
    set(d,DefaultRootWindow(d),cap,caps,8);
    assert(stereo_x11_presentation_create(d,w,UINT64_C(0x1122334455667788),&p)==0);
    assert(stereo_x11_presentation_arm(p,0,22)==-EINVAL);
    assert(stereo_x11_presentation_arm(p,UINT64_C(0x100000002),22)==0);
    assert(stereo_x11_presentation_arm(p,UINT64_C(0x100000002),22)==0);
    assert(stereo_x11_presentation_arm(p,UINT64_C(0x100000003),23)==-EAGAIN);
    assert(stereo_x11_presentation_ack(p)==-EINVAL);
    struct stereo_x11_presentation *duplicate=NULL;
    assert(stereo_x11_presentation_create(d,w,77,&duplicate)==-EBUSY&&!duplicate);
    XGetWindowProperty(d,w,req,0,12,False,AnyPropertyType,&type,&format,&count,&extra,&data);
    assert(type==XA_CARDINAL&&format==32&&count==12&&!extra);
    unsigned long expected[12]={1,1,0x11223344,0x55667788,1,2,22};
    assert(!memcmp(data,expected,sizeof expected));XFree(data);
    unsigned long values[17]={1,1,0x11223344,0x55667788,1,2,22,0,3,4,999999999,16666666,5,6,7,0,0};
    for(int n=0;n<=17;n++) {
        if(n==16)continue;
        set(d,w,res,values,n);assert(stereo_x11_presentation_read(p,NULL,&receipt)==-EPROTO);
    }
    set(d,w,res,values,16);
    assert(stereo_x11_presentation_read(p,NULL,&receipt)==1);
    assert(receipt.session==UINT64_C(0x1122334455667788)&&receipt.request==UINT64_C(0x100000002));
    assert(receipt.seconds==UINT64_C(0x300000004)&&receipt.sequence==UINT64_C(0x500000006));
    assert(receipt.refresh==16666666&&receipt.nanoseconds==999999999&&receipt.flags==7);
    XEvent e={0};e.type=PropertyNotify;e.xproperty.display=d;e.xproperty.window=w;e.xproperty.atom=res;e.xproperty.state=PropertyNewValue;
    assert(stereo_x11_presentation_read(p,&e,&receipt)==1);
    e.xproperty.window=DefaultRootWindow(d);assert(stereo_x11_presentation_read(p,&e,&receipt)==0);
    values[3]++;set(d,w,res,values,16);assert(stereo_x11_presentation_read(p,NULL,&receipt)==0);values[3]--;
    values[15]=1;set(d,w,res,values,16);assert(stereo_x11_presentation_read(p,NULL,&receipt)==-EPROTO);values[15]=0;
    values[10]=1000000000;set(d,w,res,values,16);assert(stereo_x11_presentation_read(p,NULL,&receipt)==-EPROTO);values[10]=999999999;
    XChangeProperty(d,w,res,XA_STRING,8,PropModeReplace,(unsigned char*)values,16);
    assert(stereo_x11_presentation_read(p,NULL,&receipt)==-EPROTO);
    values[10]=0;
    for(unsigned outcome=0;outcome<=4;outcome+=4) {
        values[1]=outcome;set(d,w,res,values,16);assert(stereo_x11_presentation_read(p,NULL,&receipt)==-EPROTO);
    }
    for(unsigned outcome=2;outcome<=3;outcome++) {
        values[1]=outcome;values[7]=(outcome==2?3:9);memset(values+8,0,8*sizeof(*values));
        for(unsigned i=8;i<15;i++) {
            values[i]=1;set(d,w,res,values,16);assert(stereo_x11_presentation_read(p,NULL,&receipt)==-EPROTO);values[i]=0;
        }
        unsigned low=(outcome==2?0:8),high=(outcome==2?9:13);
        values[7]=low;set(d,w,res,values,16);assert(stereo_x11_presentation_read(p,NULL,&receipt)==-EPROTO);
        values[7]=high;set(d,w,res,values,16);assert(stereo_x11_presentation_read(p,NULL,&receipt)==-EPROTO);
    }
    assert(stereo_x11_presentation_ack(p)==0);
    assert(stereo_x11_presentation_read(p,NULL,&receipt)==0);
    assert(stereo_x11_presentation_arm(p,UINT64_C(0x100000002),22)==-EINVAL);
    assert(stereo_x11_presentation_arm(p,UINT64_C(0x100000003),22)==0);
    assert(stereo_x11_presentation_cancel(p)==0);
    values[1]=2;values[5]=3;values[7]=3;memset(values+8,0,8*sizeof(*values));set(d,w,res,values,16);
    assert(stereo_x11_presentation_read(p,NULL,&receipt)==1&&receipt.outcome==2&&receipt.reason==3);
    assert(stereo_x11_presentation_ack(p)==0);
    stereo_x11_presentation_close(p);
    XGetWindowProperty(d,w,req,0,12,False,AnyPropertyType,&type,&format,&count,&extra,&data);
    assert(type==None);XFree(data);
    XGetWindowProperty(d,w,res,0,16,False,AnyPropertyType,&type,&format,&count,&extra,&data);
    assert(type==None);XFree(data);
    XDestroyWindow(d,w);XCloseDisplay(d);dlclose(lib);
    puts("PASS additive receipt API: old-server no-write, bounded flow, exact mailbox, identity and malformed controls");
    return 0;
}
