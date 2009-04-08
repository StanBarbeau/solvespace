#include "solvespace.h"
#include <png.h>

void SolveSpace::ExportSectionTo(char *filename) {
    Vector gn = (SS.GW.projRight).Cross(SS.GW.projUp);
    gn = gn.WithMagnitude(1);

    Group *g = SS.GetGroup(SS.GW.activeGroup);
    if(g->runningMesh.l.n == 0) {
        Error("No solid model present; draw one with extrudes and revolves, "
              "or use Export 2d View to export bare lines and curves.");
        return;
    }
    
    // The plane in which the exported section lies; need this because we'll
    // reorient from that plane into the xy plane before exporting.
    Vector origin, u, v, n;
    double d;

    SS.GW.GroupSelection();
#define gs (SS.GW.gs)
    if((gs.n == 0 && g->activeWorkplane.v != Entity::FREE_IN_3D.v)) {
        Entity *wrkpl = SS.GetEntity(g->activeWorkplane);
        origin = wrkpl->WorkplaneGetOffset();
        n = wrkpl->Normal()->NormalN();
        u = wrkpl->Normal()->NormalU();
        v = wrkpl->Normal()->NormalV();
    } else if(gs.n == 1 && gs.faces == 1) {
        Entity *face = SS.GetEntity(gs.entity[0]);
        origin = face->FaceGetPointNum();
        n = face->FaceGetNormalNum();
        if(n.Dot(gn) < 0) n = n.ScaledBy(-1);
        u = n.Normal(0);
        v = n.Normal(1);
    } else if(gs.n == 3 && gs.vectors == 2 && gs.points == 1) {
        Vector ut = SS.GetEntity(gs.entity[0])->VectorGetNum(),
               vt = SS.GetEntity(gs.entity[1])->VectorGetNum();
        ut = ut.WithMagnitude(1);
        vt = vt.WithMagnitude(1);

        if(fabs(SS.GW.projUp.Dot(vt)) < fabs(SS.GW.projUp.Dot(ut))) {
            SWAP(Vector, ut, vt);
        }
        if(SS.GW.projRight.Dot(ut) < 0) ut = ut.ScaledBy(-1);
        if(SS.GW.projUp.   Dot(vt) < 0) vt = vt.ScaledBy(-1);

        origin = SS.GetEntity(gs.point[0])->PointGetNum();
        n = ut.Cross(vt);
        u = ut.WithMagnitude(1);
        v = (n.Cross(u)).WithMagnitude(1);
    } else {
        Error("Bad selection for export section. Please select:\r\n\r\n"
              "    * nothing, with an active workplane "
                        "(workplane is section plane)\r\n"
              "    * a face (section plane through face)\r\n"
              "    * a point and two line segments "
                        "(plane through point and parallel to lines)\r\n");
        return;
    }
    SS.GW.ClearSelection();

    n = n.WithMagnitude(1);
    d = origin.Dot(n);

    SMesh m;
    ZERO(&m);
    m.MakeFromCopy(&(g->runningMesh));

    // Delete all triangles in the mesh that do not lie in our export plane.
    m.l.ClearTags();
    int i;
    for(i = 0; i < m.l.n; i++) {
        STriangle *tr = &(m.l.elem[i]);

        if((fabs(n.Dot(tr->a) - d) >= LENGTH_EPS) ||
           (fabs(n.Dot(tr->b) - d) >= LENGTH_EPS) ||
           (fabs(n.Dot(tr->c) - d) >= LENGTH_EPS))
        {
            tr->tag  = 1;
        }
    }
    m.l.RemoveTagged();

    // Select the naked edges in our resulting open mesh.
    SKdNode *root = SKdNode::From(&m);
    SEdgeList el;
    ZERO(&el);
    root->MakeNakedEdgesInto(&el);
    m.Clear();

    // And write the edges.
    VectorFileWriter *out = VectorFileWriter::ForFile(filename);
    if(out) {
        // parallel projection (no perspective), and no mesh
        ExportLinesAndMesh(&el, NULL,
                           u, v, n, origin, 0,
                           out);
    }
    el.Clear();
}

void SolveSpace::ExportViewTo(char *filename) {
    int i;
    SEdgeList edges;
    ZERO(&edges);
    for(i = 0; i < SS.entity.n; i++) {
        Entity *e = &(SS.entity.elem[i]);
        if(!e->IsVisible()) continue;

        e->GenerateEdges(&edges);
    }
    
    SMesh *sm = NULL;
    if(SS.GW.showShaded) {
        sm = &((SS.GetGroup(SS.GW.activeGroup))->runningMesh);
    }

    if(SS.GW.showEdges) {
        SEdgeList *selr = &((SS.GetGroup(SS.GW.activeGroup))->runningEdges);
        SEdge *se;
        for(se = selr->l.First(); se; se = selr->l.NextAfter(se)) {
            edges.AddEdge(se->a, se->b);
        }
    }

    Vector u = SS.GW.projRight,
           v = SS.GW.projUp,
           n = u.Cross(v),
           origin = SS.GW.offset.ScaledBy(-1);

    VectorFileWriter *out = VectorFileWriter::ForFile(filename);
    if(out) {
        ExportLinesAndMesh(&edges, sm, 
                           u, v, n, origin, SS.cameraTangent*SS.GW.scale,
                           out);
    }
    edges.Clear();
}

void SolveSpace::ExportLinesAndMesh(SEdgeList *sel, SMesh *sm,
                                    Vector u, Vector v, Vector n,
                                        Vector origin, double cameraTan,
                                    VectorFileWriter *out)
{
    double s = 1.0 / SS.exportScale;

    // Project into the export plane; so when we're done, z doesn't matter,
    // and x and y are what goes in the DXF.
    SEdge *e;
    for(e = sel->l.First(); e; e = sel->l.NextAfter(e)) {
        // project into the specified csys, and apply export scale
        (e->a) = e->a.InPerspective(u, v, n, origin, cameraTan).ScaledBy(s);
        (e->b) = e->b.InPerspective(u, v, n, origin, cameraTan).ScaledBy(s);
    }

    // If cutter radius compensation is requested, then perform it now
    if(fabs(SS.exportOffset) > LENGTH_EPS) {
        // assemble those edges into a polygon, and clear the edge list
        SPolygon sp;
        ZERO(&sp);
        sel->AssemblePolygon(&sp, NULL);
        sel->Clear();

        SPolygon compd;
        ZERO(&compd);
        sp.normal = Vector::From(0, 0, -1);
        sp.FixContourDirections();
        sp.OffsetInto(&compd, SS.exportOffset);
        sp.Clear();

        compd.MakeEdgesInto(sel);
        compd.Clear();
    }

    // Now the triangle mesh; project, then build a BSP to perform
    // occlusion testing and generated the shaded surfaces.
    SMesh smp;
    ZERO(&smp);
    if(sm) {
        Vector l0 = (SS.lightDir[0]).WithMagnitude(1),
               l1 = (SS.lightDir[1]).WithMagnitude(1);
        STriangle *tr;
        for(tr = sm->l.First(); tr; tr = sm->l.NextAfter(tr)) {
            STriangle tt = *tr;
            tt.a = (tt.a).InPerspective(u, v, n, origin, cameraTan).ScaledBy(s);
            tt.b = (tt.b).InPerspective(u, v, n, origin, cameraTan).ScaledBy(s);
            tt.c = (tt.c).InPerspective(u, v, n, origin, cameraTan).ScaledBy(s);

            // And calculate lighting for the triangle
            Vector n = tt.Normal().WithMagnitude(1);
            double lighting = SS.ambientIntensity +
                                  max(0, (SS.lightIntensity[0])*(n.Dot(l0))) +
                                  max(0, (SS.lightIntensity[1])*(n.Dot(l1)));
            double r = min(1, REDf  (tt.meta.color)*lighting),
                   g = min(1, GREENf(tt.meta.color)*lighting),
                   b = min(1, BLUEf (tt.meta.color)*lighting);
            tt.meta.color = RGBf(r, g, b);
            smp.AddTriangle(&tt);
        }
    }

    // Use the BSP routines to generate the split triangles in paint order.
    SBsp3 *bsp = SBsp3::FromMesh(&smp);
    SMesh sms;
    ZERO(&sms);
    bsp->GenerateInPaintOrder(&sms);
    // And cull the back-facing triangles
    STriangle *tr;
    sms.l.ClearTags();
    for(tr = sms.l.First(); tr; tr = sms.l.NextAfter(tr)) {
        Vector n = tr->Normal();
        if(n.z < 0) {
            tr->tag = 1;
        }
    }
    sms.l.RemoveTagged();

    // And now we perform hidden line removal if requested
    SEdgeList hlrd;
    ZERO(&hlrd);
    if(sm && !SS.GW.showHdnLines) {
        SKdNode *root = SKdNode::From(&smp);

        // Generate the edges where a curved surface turns from front-facing
        // to back-facing.
        if(SS.GW.showEdges) {
            root->MakeTurningEdgesInto(sel);
        }

        root->ClearTags();
        int cnt = 1234;

        SEdge *se;
        for(se = sel->l.First(); se; se = sel->l.NextAfter(se)) {
            SEdgeList out;
            ZERO(&out);
            // Split the original edge against the mesh
            out.AddEdge(se->a, se->b);
            root->OcclusionTestLine(*se, &out, cnt);
            // the occlusion test splits unnecessarily; so fix those
            out.MergeCollinearSegments(se->a, se->b);
            cnt++;
            // And add the results to our output
            SEdge *sen;
            for(sen = out.l.First(); sen; sen = out.l.NextAfter(sen)) {
                hlrd.AddEdge(sen->a, sen->b);
            }
            out.Clear();
        }

        sel = &hlrd;
    }

    // Now write the lines and triangles to the output file
    out->Output(sel, &sms);

    smp.Clear();
    sms.Clear();
    hlrd.Clear();
}

bool VectorFileWriter::StringEndsIn(char *str, char *ending) {
    int i, ls = strlen(str), le = strlen(ending);

    if(ls < le) return false;
        
    for(i = 0; i < le; i++) {
        if(tolower(ending[le-i-1]) != tolower(str[ls-i-1])) {
            return false;
        }
    }
    return true;
}

VectorFileWriter *VectorFileWriter::ForFile(char *filename) {
    VectorFileWriter *ret;
    if(StringEndsIn(filename, ".dxf")) {
        static DxfFileWriter DxfWriter;
        ret = &DxfWriter;
    } else if(StringEndsIn(filename, ".ps") || StringEndsIn(filename, ".eps")) {
        static EpsFileWriter EpsWriter;
        ret = &EpsWriter;
    } else if(StringEndsIn(filename, ".svg")) {
        static SvgFileWriter SvgWriter;
        ret = &SvgWriter;
    } else if(StringEndsIn(filename, ".plt")||StringEndsIn(filename, ".hpgl")) {
        static HpglFileWriter HpglWriter;
        ret = &HpglWriter;
    } else {
        Error("Can't identify output file type from file extension of "
        "filename '%s'; try .dxf, .svg, .plt, .hpgl, .eps, or .ps.", filename);
        return NULL;
    }

    FILE *f = fopen(filename, "wb");
    if(!f) {
        Error("Couldn't write to '%s'", filename);
        return NULL;
    }
    ret->f = f;
    return ret;
}

void VectorFileWriter::Output(SEdgeList *sel, SMesh *sm) {
    STriangle *tr;
    SEdge *e;

    // First calculate the bounding box.
    ptMin = Vector::From(VERY_POSITIVE, VERY_POSITIVE, VERY_POSITIVE);
    ptMax = Vector::From(VERY_NEGATIVE, VERY_NEGATIVE, VERY_NEGATIVE);
    if(sel) {
        for(e = sel->l.First(); e; e = sel->l.NextAfter(e)) {
            (e->a).MakeMaxMin(&ptMax, &ptMin);
            (e->b).MakeMaxMin(&ptMax, &ptMin);
        }
    }
    if(sm) {
        for(tr = sm->l.First(); tr; tr = sm->l.NextAfter(tr)) {
            (tr->a).MakeMaxMin(&ptMax, &ptMin);
            (tr->b).MakeMaxMin(&ptMax, &ptMin);
            (tr->c).MakeMaxMin(&ptMax, &ptMin);
        }
    }

    StartFile();
    if(sm && SS.exportShadedTriangles) {
        for(tr = sm->l.First(); tr; tr = sm->l.NextAfter(tr)) {
            Triangle(tr);
        }
    }
    if(sel) {
        for(e = sel->l.First(); e; e = sel->l.NextAfter(e)) {
            LineSegment(e->a.x, e->a.y, e->b.x, e->b.y);
        }
    }
    FinishAndCloseFile();
}

//-----------------------------------------------------------------------------
// Routines for DXF export
//-----------------------------------------------------------------------------
void DxfFileWriter::StartFile(void) {
    // Some software, like Adobe Illustrator, insists on a header.
    fprintf(f,
"  999\r\n"
"file created by SolveSpace\r\n"
"  0\r\n"
"SECTION\r\n"
"  2\r\n"
"HEADER\r\n"
"  9\r\n"
"$ACADVER\r\n"
"  1\r\n"
"AC1006\r\n"
"  9\r\n"
"$INSBASE\r\n"
"  10\r\n"
"0.0\r\n"
"  20\r\n"
"0.0\r\n"
"  30\r\n"
"0.0\r\n"
"  9\r\n"
"$EXTMIN\r\n"
"  10\r\n"
"0.0\r\n"
"  20\r\n"
"0.0\r\n"
"  9\r\n"
"$EXTMAX\r\n"
"  10\r\n"
"10000.0\r\n"
"  20\r\n"
"10000.0\r\n"
"  0\r\n"
"ENDSEC\r\n");

    // Then start the entities.
    fprintf(f,
"  0\r\n"
"SECTION\r\n"
"  2\r\n"
"ENTITIES\r\n");
}

void DxfFileWriter::LineSegment(double x0, double y0, double x1, double y1) {
    fprintf(f,
"  0\r\n"
"LINE\r\n"
"  8\r\n"     // Layer code
"%d\r\n"
"  10\r\n"    // xA
"%.6f\r\n"
"  20\r\n"    // yA
"%.6f\r\n"
"  30\r\n"    // zA
"%.6f\r\n"
"  11\r\n"    // xB
"%.6f\r\n"
"  21\r\n"    // yB
"%.6f\r\n"
"  31\r\n"    // zB
"%.6f\r\n",
                    0,
                    x0, y0, 0.0,
                    x1, y1, 0.0);
}
void DxfFileWriter::Triangle(STriangle *tr) {
}

void DxfFileWriter::FinishAndCloseFile(void) {
    fprintf(f,
"  0\r\n"
"ENDSEC\r\n"
"  0\r\n"
"EOF\r\n" );
    fclose(f);
}

//-----------------------------------------------------------------------------
// Routines for EPS output
//-----------------------------------------------------------------------------
double EpsFileWriter::MmToPoints(double mm) {
    // 72 points in an inch
    return (mm/25.4)*72;
}

void EpsFileWriter::StartFile(void) {
    fprintf(f,
"%%!PS-Adobe-2.0\r\n"
"%%%%Creator: SolveSpace\r\n"
"%%%%Title: title\r\n"
"%%%%Pages: 0\r\n"
"%%%%PageOrder: Ascend\r\n"
"%%%%BoundingBox: 0 0 %d %d\r\n"
"%%%%HiResBoundingBox: 0 0 %.3f %.3f\r\n"
"%%%%EndComments\r\n"
"\r\n"
"gsave\r\n"
"\r\n",
            (int)ceil(MmToPoints(ptMax.x - ptMin.x)),
            (int)ceil(MmToPoints(ptMax.y - ptMin.y)),
            MmToPoints(ptMax.x - ptMin.x),
            MmToPoints(ptMax.y - ptMin.y));
}

void EpsFileWriter::LineSegment(double x0, double y0, double x1, double y1) {
    fprintf(f,
"newpath\r\n"
"    %.3f %.3f moveto\r\n"
"    %.3f %.3f lineto\r\n"
"    1 setlinewidth\r\n"
"    0 setgray\r\n"
"stroke\r\n",
            MmToPoints(x0 - ptMin.x), MmToPoints(y0 - ptMin.y),
            MmToPoints(x1 - ptMin.x), MmToPoints(y1 - ptMin.y));
}
void EpsFileWriter::Triangle(STriangle *tr) {
    fprintf(f,
"%.3f %.3f %.3f setrgbcolor\r\n"
"newpath\r\n"
"    %.3f %.3f moveto\r\n"
"    %.3f %.3f lineto\r\n"
"    %.3f %.3f lineto\r\n"
"    closepath\r\n"
"fill\r\n",
            REDf(tr->meta.color), GREENf(tr->meta.color), BLUEf(tr->meta.color),
            MmToPoints(tr->a.x - ptMin.x), MmToPoints(tr->a.y - ptMin.y),
            MmToPoints(tr->b.x - ptMin.x), MmToPoints(tr->b.y - ptMin.y),
            MmToPoints(tr->c.x - ptMin.x), MmToPoints(tr->c.y - ptMin.y));

    // same issue with cracks, stroke it to avoid them
    double sw = max(ptMax.x - ptMin.x, ptMax.y - ptMin.y) / 1000;
    fprintf(f,
"%.3f %.3f %.3f setrgbcolor\r\n"
"%.3f setlinewidth\r\n"
"newpath\r\n"
"    %.3f %.3f moveto\r\n"
"    %.3f %.3f lineto\r\n"
"    %.3f %.3f lineto\r\n"
"    closepath\r\n"
"stroke\r\n",
            REDf(tr->meta.color), GREENf(tr->meta.color), BLUEf(tr->meta.color),
            MmToPoints(sw),
            MmToPoints(tr->a.x - ptMin.x), MmToPoints(tr->a.y - ptMin.y),
            MmToPoints(tr->b.x - ptMin.x), MmToPoints(tr->b.y - ptMin.y),
            MmToPoints(tr->c.x - ptMin.x), MmToPoints(tr->c.y - ptMin.y));
}

void EpsFileWriter::FinishAndCloseFile(void) {
    fprintf(f,
"\r\n"
"grestore\r\n"
"\r\n");
    fclose(f);
}

//-----------------------------------------------------------------------------
// Routines for SVG output
//-----------------------------------------------------------------------------
void SvgFileWriter::StartFile(void) {
    fprintf(f,
"<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\" "
    "\"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\r\n"
"<svg xmlns=\"http://www.w3.org/2000/svg\"  "
    "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
    "width='%.3fmm' height='%.3fmm' "
    "viewBox=\"0 0 %.3f %.3f\">\r\n"
"\r\n"
"<title>Exported SVG</title>\r\n"
"\r\n",
        ptMax.x - ptMin.x, ptMax.y - ptMin.y,
        ptMax.x - ptMin.x, ptMax.y - ptMin.y);
}

void SvgFileWriter::LineSegment(double x0, double y0, double x1, double y1) {
    // SVG uses a coordinate system with the origin at top left, +y down
    fprintf(f,
"<polyline points='%.3f,%.3f %.3f,%.3f' "
    "stroke-width='1' stroke='black' style='fill: none;' />\r\n",
            (x0 - ptMin.x), (ptMax.y - y0),
            (x1 - ptMin.x), (ptMax.y - y1));
}
void SvgFileWriter::Triangle(STriangle *tr) {
    // crispEdges turns of anti-aliasing, which tends to cause hairline
    // cracks between triangles; but there still is some cracking, so
    // specify a stroke width too, hope for around a pixel
    double sw = max(ptMax.x - ptMin.x, ptMax.y - ptMin.y) / 1000;
    fprintf(f,
"<polygon points='%.3f,%.3f %.3f,%.3f %.3f,%.3f' "
    "stroke='#%02x%02x%02x' stroke-width='%.3f' "
    "style='fill:#%02x%02x%02x' shape-rendering='crispEdges'/>\r\n",
            (tr->a.x - ptMin.x), (ptMax.y - tr->a.y),
            (tr->b.x - ptMin.x), (ptMax.y - tr->b.y),
            (tr->c.x - ptMin.x), (ptMax.y - tr->c.y),
            RED(tr->meta.color), GREEN(tr->meta.color), BLUE(tr->meta.color),
            sw,
            RED(tr->meta.color), GREEN(tr->meta.color), BLUE(tr->meta.color));
}

void SvgFileWriter::FinishAndCloseFile(void) {
    fprintf(f, "\r\n</svg>\r\n");
    fclose(f);
}

//-----------------------------------------------------------------------------
// Routines for HPGL output
//-----------------------------------------------------------------------------
double HpglFileWriter::MmToHpglUnits(double mm) {
    return mm*40;
}

void HpglFileWriter::StartFile(void) {
    fprintf(f, "IN;\r\n");
    fprintf(f, "SP1;\r\n");
}

void HpglFileWriter::LineSegment(double x0, double y0, double x1, double y1) {
    fprintf(f, "PU%d,%d;\r\n", (int)MmToHpglUnits(x0), (int)MmToHpglUnits(y0));
    fprintf(f, "PD%d,%d;\r\n", (int)MmToHpglUnits(x1), (int)MmToHpglUnits(y1));
}
void HpglFileWriter::Triangle(STriangle *tr) {
    // HPGL does not support filled triangles
}

void HpglFileWriter::FinishAndCloseFile(void) {
    fclose(f);
}

//-----------------------------------------------------------------------------
// Export the mesh as an STL file; it should always be vertex-to-vertex and
// not self-intersecting, so not much to do.
//-----------------------------------------------------------------------------
void SolveSpace::ExportMeshTo(char *filename) {
    SMesh *m = &(SS.GetGroup(SS.GW.activeGroup)->runningMesh);
    if(m->l.n == 0) {
        Error("Active group mesh is empty; nothing to export.");
        return;
    }

    FILE *f = fopen(filename, "wb");
    if(!f) {
        Error("Couldn't write to '%s'", filename);
        return;
    }
    char str[80];
    memset(str, 0, sizeof(str));
    strcpy(str, "STL exported mesh");
    fwrite(str, 1, 80, f);

    DWORD n = m->l.n;
    fwrite(&n, 4, 1, f);

    double s = SS.exportScale;
    int i;
    for(i = 0; i < m->l.n; i++) {
        STriangle *tr = &(m->l.elem[i]);
        Vector n = tr->Normal().WithMagnitude(1);
        float w;
        w = (float)n.x;           fwrite(&w, 4, 1, f);
        w = (float)n.y;           fwrite(&w, 4, 1, f);
        w = (float)n.z;           fwrite(&w, 4, 1, f);
        w = (float)((tr->a.x)/s); fwrite(&w, 4, 1, f);
        w = (float)((tr->a.y)/s); fwrite(&w, 4, 1, f);
        w = (float)((tr->a.z)/s); fwrite(&w, 4, 1, f);
        w = (float)((tr->b.x)/s); fwrite(&w, 4, 1, f);
        w = (float)((tr->b.y)/s); fwrite(&w, 4, 1, f);
        w = (float)((tr->b.z)/s); fwrite(&w, 4, 1, f);
        w = (float)((tr->c.x)/s); fwrite(&w, 4, 1, f);
        w = (float)((tr->c.y)/s); fwrite(&w, 4, 1, f);
        w = (float)((tr->c.z)/s); fwrite(&w, 4, 1, f);
        fputc(0, f);
        fputc(0, f);
    }

    fclose(f);
}

//-----------------------------------------------------------------------------
// Export a view of the model as an image; we just take a screenshot, by
// rendering the view in the usual way and then copying the pixels.
//-----------------------------------------------------------------------------
void SolveSpace::ExportAsPngTo(char *filename) {
    int w = (int)SS.GW.width, h = (int)SS.GW.height;
    // No guarantee that the back buffer contains anything valid right now,
    // so repaint the scene. And hide the toolbar too.
    int prevShowToolbar = SS.showToolbar;
    SS.showToolbar = false;
    SS.GW.Paint(w, h);
    SS.showToolbar = prevShowToolbar;
    
    FILE *f = fopen(filename, "wb");
    if(!f) goto err;

    png_struct *png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
        NULL, NULL, NULL);
    if(!png_ptr) goto err;

    png_info *info_ptr = png_create_info_struct(png_ptr);
    if(!png_ptr) goto err;

    if(setjmp(png_jmpbuf(png_ptr))) goto err;

    png_init_io(png_ptr, f);

    // glReadPixels wants to align things on 4-boundaries, and there's 3
    // bytes per pixel. As long as the row width is divisible by 4, all
    // works out.
    w &= ~3; h &= ~3;

    png_set_IHDR(png_ptr, info_ptr, w, h,
        8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    
    png_write_info(png_ptr, info_ptr);

    // Get the pixel data from the framebuffer
    BYTE *pixels = (BYTE *)AllocTemporary(3*w*h);
    BYTE **rowptrs = (BYTE **)AllocTemporary(h*sizeof(BYTE *));
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    int y;
    for(y = 0; y < h; y++) {
        // gl puts the origin at lower left, but png puts it top left
        rowptrs[y] = pixels + ((h - 1) - y)*(3*w);
    }
    png_write_image(png_ptr, rowptrs);

    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(f);
    return;

err:    
    Error("Error writing PNG file '%s'", filename);
    if(f) fclose(f);
    return;
}

