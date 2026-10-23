"""Render electrical SVG documents with system librsvg/Cairo (vector PDF)."""
from pathlib import Path
import ctypes as C
import xml.etree.ElementTree as ET


def render_pages(pages, pdf_path):
    r=C.CDLL('librsvg-2.so.2');c=C.CDLL('libcairo.so.2');g=C.CDLL('libgobject-2.0.so.0')
    P=C.c_void_p;D=C.c_double;I=C.c_int
    def bind(lib,name,args,ret):
        f=getattr(lib,name);f.argtypes=args;f.restype=ret;return f
    class Rect(C.Structure):
        _fields_=[('x',D),('y',D),('width',D),('height',D)]
    new=bind(r,'rsvg_handle_new_from_data',[P,C.c_size_t,C.POINTER(P)],P)
    render=bind(r,'rsvg_handle_render_document',[P,P,C.POINTER(Rect),C.POINTER(P)],I)
    pdf=bind(c,'cairo_pdf_surface_create',[C.c_char_p,D,D],P)
    png=bind(c,'cairo_image_surface_create',[I,I,I],P)
    create=bind(c,'cairo_create',[P],P);destroy=bind(c,'cairo_destroy',[P],None)
    finish=bind(c,'cairo_surface_finish',[P],None);sdestroy=bind(c,'cairo_surface_destroy',[P],None)
    status=bind(c,'cairo_status',[P],I);sstatus=bind(c,'cairo_surface_status',[P],I)
    writepng=bind(c,'cairo_surface_write_to_png',[P,C.c_char_p],I)
    showpage=bind(c,'cairo_show_page',[P],None);unref=bind(g,'g_object_unref',[P],None)
    size=bind(c,'cairo_pdf_surface_set_size',[P,D,D],None)
    surface=pdf(str(pdf_path).encode(),1190.55,841.89);ctx=create(surface)
    for path,svgtext in pages:
        path=Path(path);path.parent.mkdir(parents=True,exist_ok=True);path.write_text(svgtext)
        root=ET.fromstring(svgtext);_,_,w,h=map(float,root.attrib['viewBox'].split())
        pw,ph=(1190.55,841.89) if w>=h else (841.89,1190.55)
        size(surface,pw,ph)
        svg=svgtext.encode();buf=C.create_string_buffer(svg);err=P()
        handle=new(buf,len(svg),C.byref(err));assert handle and not err.value,path
        viewport=Rect(0,0,pw,ph)
        assert render(handle,ctx,C.byref(viewport),C.byref(err)) and not err.value,path
        assert status(ctx)==0;showpage(ctx)
        ps=png(0,int(w),int(h));pc=create(ps);viewport=Rect(0,0,w,h)
        assert render(handle,pc,C.byref(viewport),C.byref(err)) and not err.value,path
        assert writepng(ps,str(path.with_suffix('.png')).encode())==0,path
        finish(ps);assert sstatus(ps)==0;destroy(pc);sdestroy(ps);unref(handle)
    finish(surface);assert sstatus(surface)==0;destroy(ctx);sdestroy(surface)


def render_svg(path):
    path=Path(path)
    render_pages([(path,path.read_text())],path.with_suffix('.pdf'))
