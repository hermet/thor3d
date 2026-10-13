#include <thorvg_toolkit.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace tvg;
using namespace tvg::toolkit;

namespace {
#include "e1m1.h"
#include "lights.h"
constexpr float doorMaxOpen = 0.95f;
constexpr float pi = 3.14159265f;
struct Door { int x, y, plane; float open = 0; bool target = false; float hold = 3.0f; };
struct Hit { float distance, u; char tile; int side; bool edge = false; };

struct Wolf : App {
    std::array<bool, 512> keys{};
    std::vector<Door> doors;
    std::array<Shape*, 136> layers{};
    Shape* overlay = nullptr;
    Shape* ceiling = nullptr;
    Shape* floor = nullptr;
    Size renderedSize{};
    Scene* foreground = nullptr;
    Scene* lighting = nullptr;
    Scene* vegetation = nullptr;
    struct PlantVisual { Scene *root, *art, *wellArt; Shape* clip; std::array<Shape*, 24> wellColors{}; };
    std::array<PlantVisual, (sizeof(plants)/sizeof(plants[0]))+(sizeof(wells)/sizeof(wells[0]))> plantVisuals{};
    std::array<std::array<uint8_t, 3>, 136> colors{};
    Scene* mapScene = nullptr;
    Shape* mapWalls = nullptr;
    Shape* mapDoors = nullptr;
    Shape* mapPlayer = nullptr;
    bool mapDoorsDirty = true;
    std::array<Shape*, 10> gunShapes{};
    Shape* muzzle = nullptr;
    struct LampVisual {
        Scene* fixture;
        Shape *hanger, *shade, *bulb, *clip, *pool, *floorClip;
        RadialGradient *green, *glow;
    };
    std::array<LampVisual, sizeof(lamps)/sizeof(lamps[0])> lampVisuals{};
    float px = startX, py = startY, angle = startQuarterTurns*pi/2;
    size_t previous = 0;
    bool minimap = true, dirty = true;
    float shot = 0;

    Wolf() : App("Thor3D", {1920, 1080}) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (map[y][x]=='|' || map[y][x]=='-') doors.push_back({x,y,map[y][x]=='-' ? 1 : 0});
            }
        }
    }
    static int index(Key k) {
        int v = static_cast<int>(k);
        return (v & (1 << 30)) ? 256 + (v & 255) : (v & 255);
    }
    bool held(Key k) const { return keys[index(k)]; }
    char tile(int x, int y) const {
        if (x < 0 || y < 0 || x >= W || y >= H) return '#';
        char t=map[y][x];
        if (t=='#') {
            for (auto& cell : prisonWalls) {
                if (cell[0]==x && cell[1]==y) return 'P';
            }
        }
        return t=='|' || t=='-' ? 'D' : t;
    }
    bool wall(int x,int y) const {
        char t=tile(x,y);
        return t=='#' || t=='G' || t=='W' || t=='P';
    }
    Door* door(int x, int y) {
        for (auto& d : doors) {
            if (d.x == x && d.y == y) return &d;
        }
        return nullptr;
    }
    bool solid(int x, int y) {
        char t = tile(x, y);
        if (t == '.') {
            for (auto& plant : plants) {
                if (plant[0]==x && plant[1]==y) return true;
            }
            for (auto& well : wells) {
                if (well[0]==x && well[1]==y) return true;
            }
            return false;
        }
        if (t == 'D') return door(x, y)->open < doorMaxOpen;
        return true;
    }
    bool fits(float x, float y) {
        constexpr float r = 0.20f;
        for (int yy = int(std::floor(y-r)); yy <= int(std::floor(y+r)); ++yy) {
            for (int xx = int(std::floor(x-r)); xx <= int(std::floor(x+r)); ++xx) {
                if (solid(xx, yy)) return false;
            }
        }
        return true;
    }
    Hit cast(float dx, float dy) {
        int x = int(px), y = int(py);
        float ax = std::abs(dx) < 0.00001f ? 1e8f : std::abs(1 / dx);
        float ay = std::abs(dy) < 0.00001f ? 1e8f : std::abs(1 / dy);
        int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
        float tx = (dx < 0 ? px-x : x+1-px) * ax;
        float ty = (dy < 0 ? py-y : y+1-py) * ay;
        for (int i = 0; i <= 128; ++i) {
            int side;
            float dist;
            if (i==0 && tile(x,y)=='D') { dist=0; side=0; }
            else if (tx < ty) { dist = tx; tx += ax; x += sx; side = 0; }
            else { dist = ty; ty += ay; y += sy; side = 1; }
            char t = tile(x, y);
            if (t == '.') continue;
            float u = side ? px + dist * dx : py + dist * dy;
            u -= std::floor(u);
            if (t == 'D') {
                auto d = door(x, y);
                if (d->open>=1.0f) continue;
                // A sliding slab with 0.12 tile thickness; the portion beyond the
                // doorway is recessed into the wall rather than drawn over it.
                constexpr float halfThickness=0.06f;
                float minX=x+(d->plane==0 ? 0.5f-halfThickness : d->open);
                float maxX=x+(d->plane==0 ? 0.5f+halfThickness : 1.0f);
                float minY=y+(d->plane==1 ? 0.5f-halfThickness : d->open);
                float maxY=y+(d->plane==1 ? 0.5f+halfThickness : 1.0f);
                float near=-1e8f, far=1e8f;
                int normal=d->plane;
                auto slab=[&](float origin,float direction,float lo,float hi,int axis) {
                    if (std::abs(direction)<0.00001f) return origin>=lo && origin<=hi;
                    float first=(lo-origin)/direction, last=(hi-origin)/direction;
                    if (first>last) std::swap(first,last);
                    if (first>near) { near=first; normal=axis; }
                    far=std::min(far,last);
                    return near<=far;
                };
                if (!slab(px,dx,minX,maxX,0) || !slab(py,dy,minY,maxY,1)) continue;
                if (near<dist-0.0001f || near>std::min(tx,ty)+0.0001f) continue;
                float cross=d->plane==1 ? px+near*dx-x : py+near*dy-y;
                return {std::max(near,0.05f),cross-d->open,t,normal,normal!=d->plane};
            }
            return {std::max(dist, 0.05f), u, t, side};
        }
        return {100, 0, '#', 0};
    }
    void rect(int layer, float x, float y, float w, float h) {
        if (w <= 0 || h <= 0) return;
        layers[layer]->appendRect(x, y, w, h);
    }
    void use() {
        // Only interact with the first surface in front, so walls block interaction.
        auto hit = cast(std::cos(angle), std::sin(angle));
        if (hit.tile == 'D' && hit.distance < 1.6f) {
            auto d = door(int(px+std::cos(angle)*(hit.distance+0.01f)),
                          int(py+std::sin(angle)*(hit.distance+0.01f)));
            if (d) { d->target = !d->target; d->hold=3.0f; }
        } else {
            // Fully open doors are invisible to the ray; allow closing the adjacent one.
            for (auto& d : doors) {
                float dx=d.x+0.5f-px, dy=d.y+0.5f-py;
                if (dx*dx+dy*dy < 2.0f && dx*std::cos(angle)+dy*std::sin(angle)>0.3f) {
                    d.target = !d.target; d.hold=3.0f;
                    break;
                }
            }
        }
        dirty = true;
    }
    bool keydown(Canvas*, Key k) override {
        if (held(k)) return false;
        keys[index(k)] = true;
        if (k == Key::LeftControl) { shot=0.15f; dirty=true; }
        if (k == Key::Escape) quit();
        if (k == Key::Space || static_cast<int>(k) == 'e') use();
        if (k == Key::Tab) { minimap = !minimap; dirty = true; }
        if (static_cast<int>(k) == 'r') {
            px=startX; py=startY; angle=startQuarterTurns*pi/2;
            for (auto& d : doors) { d.open=0; d.target=false; d.hold=3.0f; }
            mapDoorsDirty=true;
            dirty=true;
        }
        return false;
    }
    bool keyup(Canvas*, Key k) override { keys[index(k)] = false; return false; }
    bool clickdown(Canvas*, float, float, uint8_t b) override {
        if (b == 1) { shot=0.15f; dirty=true; }
        return false;
    }
    bool content(Canvas* canvas, const Size&) override {
        ceiling = Shape::gen();
        ceiling->appendRect(0, 0, size().w, size().h*0.42f);
        ceiling->fill(46,46,48); canvas->add(ceiling);
        floor = Shape::gen();
        floor->appendRect(0, size().h*0.42f, size().w, size().h*0.58f);
        floor->fill(105,101,93); canvas->add(floor);
        for (int i=0; i<136; ++i) {
            layers[i]=Shape::gen();
            int shade=i%8;
            float light=1.0f-shade*0.085f;
            int group=i/8;
            int r=29, g=30, b=126;
            if (group==1) { r=38; g=38; b=155; }
            if (group==2) { r=22; g=25; b=34; }
            if (group==3) { r=25; g=119; b=125; }
            if (group==4) { r=37; g=44; b=44; }
            if (group==5) { r=179; g=125; b=46; }
            if (group==6) { r=46; g=46; b=48; }
            if (group==7) { r=105; g=101; b=93; }
            if (group==8) { r=54; g=157; b=157; }
            if (group==9) { r=13; g=67; b=72; }
            if (group==10) { r=119; g=130; b=126; }
            if (group==11) { r=162; g=163; b=157; }
            if (group==12) { r=213; g=214; b=206; }
            if (group==13) { r=45; g=46; b=45; }
            if (group==14) { r=137; g=96; b=48; }
            if (group==15) { r=184; g=133; b=67; }
            if (group==16) { r=55; g=38; b=21; }
            colors[i] = {uint8_t(r*light),uint8_t(g*light),uint8_t(b*light)};
            layers[i]->fill(colors[i][0],colors[i][1],colors[i][2]);
            if (canvas->add(layers[i]) != Result::Success) return false;
        }
        lighting=Scene::gen(); canvas->add(lighting);
        for (auto& lamp : lampVisuals) {
            lamp.fixture=Scene::gen(); lamp.fixture->opacity(0);
            lamp.hanger=Shape::gen(); lamp.hanger->fill(104,115,101);
            lamp.shade=Shape::gen(); lamp.green=RadialGradient::gen();
            Fill::ColorStop shadeStops[]={{0,42,221,55,255},{0.6f,13,133,32,255},{1,4,53,15,255}};
            lamp.green->colorStops(shadeStops,3); lamp.shade->fill(lamp.green);
            lamp.bulb=Shape::gen(); lamp.bulb->fill(222,255,178);
            lamp.fixture->add(lamp.hanger); lamp.fixture->add(lamp.shade); lamp.fixture->add(lamp.bulb);
            lamp.clip=Shape::gen(); lamp.clip->appendRect(0,0,1,1);
            lamp.fixture->clip(lamp.clip); lighting->add(lamp.fixture);
            lamp.pool=Shape::gen(); lamp.pool->opacity(0); lamp.glow=RadialGradient::gen();
            lamp.glow->radial(0,0,1,0,0,0);
            Fill::ColorStop stops[]={{0,239,248,218,140},{0.35f,227,241,207,110},
                                     {0.7f,213,233,197,50},{1,210,232,196,0}};
            lamp.glow->colorStops(stops,4); lamp.pool->fill(lamp.glow);
            lamp.floorClip=Shape::gen(); lamp.floorClip->appendRect(0,0,1,1);
            lamp.pool->clip(lamp.floorClip); lighting->add(lamp.pool);
        }
        vegetation=Scene::gen(); canvas->add(vegetation);
        for (auto& plant : plantVisuals) {
            plant.root=Scene::gen(); plant.root->opacity(0);
            plant.art=Scene::gen(); plant.root->add(plant.art);
            plant.wellArt=Scene::gen(); plant.root->add(plant.wellArt);
            plant.clip=Shape::gen(); plant.clip->appendRect(0,0,1,1);
            plant.root->clip(plant.clip); vegetation->add(plant.root);
            constexpr uint8_t palette[][3]={
                {86,48,22}, {125,71,30}, {168,102,45}, {208,142,70},
                {17,16,93}, {30,25,175}, {46,39,231}, {91,92,252}
            };
            for (int color=0;color<8;++color) {
                auto shape=Shape::gen(); shape->fill(palette[color][0],palette[color][1],palette[color][2]);
                for (int y=0;y<31;++y) {
                    for (int x=0;x<14;++x) {
                        int pixelColor=-1;
                        if (y<23) {
                            float radius=y<12 ? 1.0f+y*0.35f : 5.2f-(y-12)*0.3f;
                            float center=6.5f+0.5f*std::sin(y*0.8f);
                            if (std::abs(x-center)<radius && ((x*13+y*7)%11!=0)) {
                                pixelColor=(x*3+y*7)%4;
                                if (x<center-1) pixelColor=std::min(3,pixelColor+1);
                            }
                            if (x==6 && y>3) pixelColor=1;
                        } else {
                            float radius=y==23 ? 1.5f : y<29 ? 3.5f : 2.8f;
                            if (std::abs(x-6.5f)<radius) pixelColor=x<5 ? 7 : x<8 ? 6 : x<10 ? 5 : 4;
                        }
                        if (pixelColor==color) shape->appendRect((x-7)/32.0f,(y-31)/32.0f,1.0f/32,1.0f/32);
                    }
                }
                plant.art->add(shape);
            }
        }
        for (auto& visual : plantVisuals) {
            // Denser pixel geometry preserves the retro style while rounding
            // the rim and modeling staggered, beveled stones around a cylinder.
            for (int color=0;color<24;++color) {
                auto shape=Shape::gen();
                if (color<16) {
                    int gray=30+color*12;
                    shape->fill(gray,gray+2,std::max(0,gray-5));
                } else {
                    int blue=color-16;
                    shape->fill(19+blue*8,24+blue*10,94+blue*18);
                }
                for (int y=0;y<48;++y) {
                    for (int x=0;x<64;++x) {
                        float nx=(x-31.5f)/31.0f;
                        float ey=(y-8.5f)/8.0f;
                        int pixelColor=-1;
                        float cylinder=std::sqrt(std::max(0.0f,1-nx*nx));
                        if (std::abs(nx)<1 && y>=9 && y<40+6*cylinder) {
                            float arc=(std::asin(nx)+pi/2)*20;
                            float yy=y-14+0.65f*std::sin(arc*0.29f);
                            int course=int(std::floor(yy/7.5f));
                            float by=yy-course*7.5f;
                            float brick=arc+(course%2)*4.5f;
                            int block=int(std::floor(brick/9));
                            float bx=brick-block*9;
                            bool mortar=bx<0.65f || by<0.7f;
                            int lighting=int(5*cylinder-2*nx);
                            int variation=((block*13+course*7)&3)-1;
                            int tone=6+lighting+variation;
                            if (bx<1.6f || by<1.8f) tone+=2;
                            if (bx>7.6f || by>5.8f) tone-=3;
                            pixelColor=mortar ? std::clamp(1+int(cylinder*2),0,15) : std::clamp(tone,0,15);
                        }
                        if (nx*nx+ey*ey<1) {
                            float innerX=(x-31.5f)/24.5f, innerY=(y-8.5f)/4.1f;
                            if (innerX*innerX+innerY*innerY<1) {
                                float ripple=std::sin(x*0.19f+y*1.3f);
                                int reflection=(y==7 || y==8) && x>15 && x<46 ? 2 : 0;
                                pixelColor=16+std::clamp(2+int((ripple+1)*1.2f)+reflection,0,7);
                            } else {
                                float angle=std::atan2(ey,nx);
                                float segment=(angle+pi)*6/pi;
                                float seam=segment-std::floor(segment);
                                int light=12+int(-nx*2-ey);
                                pixelColor=seam<0.055f ? 4 : std::clamp(light,0,15);
                                if (nx*nx+ey*ey>0.84f) pixelColor=std::max(2,pixelColor-2);
                            }
                        }
                        if (pixelColor==color) shape->appendRect((x-32)/102.4f,(y-48)/96.0f,1.0f/102.4f,1.0f/96.0f);
                    }
                }
                visual.wellColors[color]=shape; visual.wellArt->add(shape);
            }
        }
        foreground=Scene::gen(); canvas->add(foreground);
        for (auto& shape : gunShapes) { shape=Shape::gen(); foreground->add(shape); }
        muzzle=Shape::gen();
        auto flashColor=colors[40]; muzzle->fill(flashColor[0],flashColor[1],flashColor[2]);
        foreground->add(muzzle);
        mapScene=Scene::gen(); canvas->add(mapScene);
        mapWalls=Shape::gen(); mapWalls->fill(0,0,0,0); mapWalls->strokeFill(180,74,61); mapWalls->strokeWidth(1.0f);
        mapDoors=Shape::gen(); mapDoors->fill(0,0,0,0); mapDoors->strokeFill(216,182,83); mapDoors->strokeWidth(1.0f);
        mapPlayer=Shape::gen(); mapPlayer->fill(225,226,207);
        mapScene->add(mapWalls); mapScene->add(mapDoors); mapScene->add(mapPlayer);
        overlay=Shape::gen(); overlay->fill(225,226,207);
        if (canvas->add(overlay) != Result::Success) return false;
        render();
        return true;
    }
    bool update(Canvas* canvas, size_t elapsed) override {
        dirty |= renderedSize.w!=size().w || renderedSize.h!=size().h;
        float elapsedSeconds=previous ? (elapsed-previous)*0.001f : 0;
        float dt=std::min(elapsedSeconds,0.05f);
        previous=elapsed;
        float turn=(held(Key::Right)-held(Key::Left))*2.0f*dt;
        float forward=(held(Key::Up)||held(Key('w')))-(held(Key::Down)||held(Key('s')));
        float strafe=held(Key('d'))-held(Key('a'));
        angle+=turn;
        float speed=held(Key::LeftShift)||held(Key::RightShift) ? 3.4f : 2.0f;
        float norm=std::max(1.0f,std::sqrt(forward*forward+strafe*strafe));
        float dx=(std::cos(angle)*forward-std::sin(angle)*strafe)*speed*dt/norm;
        float dy=(std::sin(angle)*forward+std::cos(angle)*strafe)*speed*dt/norm;
        if (fits(px+dx,py)) px+=dx;
        if (fits(px,py+dy)) py+=dy;
        dirty |= turn!=0 || dx!=0 || dy!=0;
        for (auto& d : doors) {
            float before=d.open;
            // Hold open while the player touches the doorway; restart the delay
            // on departure so an automatic close cannot trap the player.
            bool occupied=std::abs(px-d.x-0.5f)<0.75f && std::abs(py-d.y-0.5f)<0.75f;
            if (d.open>0 && occupied) { d.target=true; d.hold=3.0f; }
            else if (d.target && before>=doorMaxOpen) {
                d.hold=std::max(0.0f,d.hold-elapsedSeconds);
                if (d.hold==0) d.target=false;
            }
            d.open=std::clamp(d.open+(d.target ? 1 : -1)*dt*1.5f,0.0f,doorMaxOpen);
            dirty |= before!=d.open;
            mapDoorsDirty |= (before>=doorMaxOpen)!=(d.open>=doorMaxOpen);
        }
        if (shot>0) { shot=std::max(0.0f,shot-dt); dirty=true; }
        if (!dirty) return false;
        render(); canvas->update(); dirty=false;
        return true;
    }
    void render() {
        for (auto s : layers) { s->reset(); }
        overlay->reset();
        bool resized=renderedSize.w!=size().w || renderedSize.h!=size().h;
        renderedSize=size();
        float sw=size().w, sh=size().h, view=sh;
        ceiling->reset(); ceiling->appendRect(0,0,sw,view/2);
        floor->reset(); floor->appendRect(0,view/2,sw,sh-view/2);
        float projection=sw/(2*0.66f);
        float cameraCos=std::cos(angle), cameraSin=std::sin(angle);
        constexpr int columns=320;
        std::array<float, columns> depth{};
        for (int c=0; c<columns; ++c) {
            float camera=2*(c+0.5f)/columns-1;
            float dx=cameraCos-cameraSin*camera*0.66f;
            float dy=cameraSin+cameraCos*camera*0.66f;
            auto hit=cast(dx,dy);
            depth[c]=hit.distance;
            float height=projection/std::max(hit.distance,0.05f), top=(view-height)/2;
            float x=c*sw/columns, width=sw/columns+0.1f;
            int shade=std::clamp(int(hit.distance*0.45f)+hit.side*2,0,7);
            constexpr int rows = 64;
            for (int row=0; row<rows; ++row) {
                float y0=std::clamp(top+height*row/rows,0.0f,view);
                float y1=std::clamp(top+height*(row+1)/rows,0.0f,view);
                int layer;
                if (hit.tile=='W') {
                    float u=hit.u, v=row+0.5f;
                    float plank=std::fmod(u*4,1.0f);
                    float grain=std::sin(u*160+0.8f*std::sin(v*0.23f))
                                +0.4f*std::sin(u*340+v*0.13f);
                    int tone=std::clamp(shade+int((grain+1.4f)*1.5f)+int(plank*1.2f),0,7);
                    int group=14;
                    if (plank<0.07f || plank>0.96f) { group=16; tone=shade; }
                    else if (plank<0.14f) { group=15; tone=std::min(7,shade+1); }
                    if (v<2 || v>62) { group=15; tone=std::min(7,shade+(v>62 ? 2 : 0)); }
                    layer=group*8+tone;
                } else if (hit.tile=='D' && hit.edge) {
                    // Dark recessed metal on the exposed leading edge.
                    layer=72+std::min(7,shade+2);
                } else if (hit.tile=='D') {
                    float u=hit.u*64, v=row+0.5f;
                    bool frame=u<5 || u>59 || v<5 || v>59;
                    bool lip=u<7 || u>57 || v<7 || v>57;
                    int group=3;
                    int tone=shade;
                    if (frame) {
                        group=8;
                        tone=shade;
                        // A narrow highlight and a recessed edge give the frame depth.
                        if (u<1.5f || v<1.5f) tone=shade;
                        if (u>62 || v>62) { group=9; tone=shade; }
                        float rivetY=std::fmod(v-6+64,10.0f)-1.0f;
                        float rivetX=std::fmod(u-7+64,10.0f)-1.0f;
                        bool rivet=((u-2.5f)*(u-2.5f)<1.3f || (u-61.5f)*(u-61.5f)<1.3f)
                                   && rivetY*rivetY<1.2f;
                        rivet |= ((v-2.5f)*(v-2.5f)<1.3f || (v-61.5f)*(v-61.5f)<1.3f)
                                 && rivetX*rivetX<1.2f;
                        if (rivet) { group=9; tone=std::min(7,shade+2); }
                    } else if (lip) {
                        group=(u<7 || v>57) ? 8 : 9;
                        tone=std::min(7,shade+1);
                    }
                    // Metal lock plate and brass pull sit on the left of the panel.
                    if (u>15 && u<24 && v>27 && v<44) {
                        group=4; tone=shade;
                        if (u<16.5f || v<28.5f) group=10;
                        if (u>22 || v>42) tone=std::min(7,shade+3);
                        if (u>18 && u<21.5f && v>31 && v<37) {
                            group=5;
                            if (u>19.2f && u<20.5f && v>32 && v<36) group=4;
                        }
                    }
                    if (u>9.5f && u<12 && v>29 && v<40) {
                        group=5; tone=std::min(7,shade+(u>11 ? 2 : 0));
                    }
                    layer=group*8+tone;
                } else {
                    // A 64x64 stone pattern in wall coordinates: staggered blocks,
                    // uneven recessed mortar and broad, worn surface shading.
                    float u=hit.u*64, v=row+0.5f;
                    float warpedV=v+0.8f*std::sin(u*pi/16)+0.35f*std::sin(u*pi/8);
                    int course=int(std::floor(warpedV/16));
                    float by=warpedV-course*16;
                    float warpedU=u+(course%2)*8+1.1f*std::sin(v*pi/16);
                    int block=int(std::floor(warpedU/16));
                    float bx=warpedU-block*16;
                    float joint=1.7f+0.5f*std::sin((course*7+block*3)*1.3f);
                    bool mortar=bx<joint || by<2.0f || bx>15.2f;
                    float wear=std::sin(u*0.37f+v*0.19f)*std::sin(v*0.43f-u*0.13f);
                    int surfaceShade=std::clamp(shade+int(by/5)+int(bx/8)
                                               +int((wear+1)*1.2f),0,7);
                    int group=((course*3+block*5)&3)==0 ? 1 : 0;
                    if (bx<3 || by<3) group=1;
                    if (bx>12.5f || by>12.0f) surfaceShade=std::min(7,surfaceShade+2);
                    layer=mortar ? 16+shade : group*8+surfaceShade;
                    if (hit.tile=='G') {
                        // Gray stone uses pale bevels and charcoal recessed mortar.
                        int stoneShade=std::clamp(shade+int(by/8)+int(bx/12)
                                                 +int((wear+1)*0.7f),0,7);
                        int stoneGroup=(bx<3 || by<3) ? 12 : 11;
                        if (bx>12.5f || by>12.0f) stoneShade=std::min(7,stoneShade+2);
                        layer=mortar ? 104+shade : stoneGroup*8+stoneShade;
                    }
                    if (hit.tile=='P' && u>=12 && u<53 && v>=3) {
                        // Recessed iron frame around a dark cell, with six bars.
                        bool frame=u<16 || u>49 || v<7;
                        layer=frame ? 80+shade : 32+std::min(7,shade+5);
                        if (frame && (u>51 || (u<16 && u>14) || (v<7 && v>5))) layer=32+shade;
                        if (!frame) {
                            float bar=std::fmod(u-19+64,5.0f);
                            if (bar<1.4f) layer=80+std::min(7,shade+(bar>0.65f ? 3 : 1));
                        }
                        // Lock plate and keyhole on the left of the bars.
                        if (u>=13 && u<22 && v>=26 && v<43) {
                            layer=80+std::min(7,shade+1);
                            if (u>20 || v>41) layer=32+shade;
                            if (u>=17 && u<19 && v>=31 && v<37) layer=32+std::min(7,shade+5);
                        }
                    }
                }
                rect(layer,x,y0,width,y1-y0);
            }
        }
        // Lights use the same camera and wall depths as the raycast geometry.
        // This keeps fixtures and floor illumination behind closed doors and walls.
        std::array<float, columns> floorEdges{};
        bool floorVisible=false;
        float maxDepth=*std::max_element(depth.begin(),depth.end());
        for (int c=0;c<columns;++c) {
            floorEdges[c]=std::min(view,view/2+projection*0.5f/depth[c]);
            floorVisible |= floorEdges[c]<view;
        }
        for (size_t lampIndex=0;lampIndex<lampVisuals.size();++lampIndex) {
            auto lamp=lamps[lampIndex];
            auto& visual=lampVisuals[lampIndex];
            visual.fixture->opacity(0); visual.pool->opacity(0);
            float dx=lamp.x-px, dy=lamp.y-py;
            float distance=dx*cameraCos+dy*cameraSin;
            if (distance<0.2f || distance>=maxDepth) continue;
            // Fully occluded fixtures would produce an empty clip. Do not submit
            // them: the GL renderer cannot render a null clip geometry.
            float lateral=-dx*cameraSin+dy*cameraCos;
            float x=sw/2+lateral*projection/distance;
            float radius=projection*0.12f/distance;
            if (x+radius<0 || x-radius>sw) continue;
            float ceilingY=view/2-projection*0.46f/distance;
            // Build only the fixture's visible screen columns into its reusable clip.
            int first=std::clamp(int(std::floor((x-radius)*columns/sw)),0,columns-1);
            int last=std::clamp(int(std::floor((x+radius)*columns/sw)),0,columns-1);
            bool fixtureVisible=false;
            for (int c=first;c<=last;++c) { fixtureVisible |= distance<depth[c]; }
            if (fixtureVisible && ceilingY+radius*0.62f>0) {
                auto fixture=visual.fixture;
                auto shade=visual.shade;
                auto green=visual.green;
                visual.hanger->reset();
                visual.hanger->appendRect(x-radius*0.12f,ceilingY-radius*0.5f,radius*0.24f,radius*0.3f);
                shade->reset();
                shade->moveTo(x-radius,ceilingY+radius*0.45f);
                shade->cubicTo(x-radius,ceilingY,x-radius*0.5f,ceilingY-radius*0.3f,x,ceilingY-radius*0.3f);
                shade->cubicTo(x+radius*0.5f,ceilingY-radius*0.3f,x+radius,ceilingY,x+radius,ceilingY+radius*0.45f);
                shade->close();
                green->radial(x,ceilingY+radius*0.3f,radius,x,ceilingY+radius*0.3f,0);
                shade->fill(green);
                visual.bulb->reset();
                visual.bulb->appendCircle(x,ceilingY+radius*0.45f,radius*0.76f,radius*0.17f);
                visual.clip->reset();
                for (int c=first;c<=last;++c) {
                    if (distance<depth[c]) visual.clip->appendRect(c*sw/columns,0,sw/columns+0.1f,view/2);
                }
                fixture->opacity(255);
            }

            float floorY=view/2+projection*0.5f/distance;
            float rx=projection*0.48f/distance;
            float ry=projection*0.12f/(distance*distance);
            if (floorY-ry>view || !floorVisible) continue;
            auto pool=visual.pool;
            pool->reset(); pool->appendCircle(x,floorY,rx,ry);
            Matrix stretch={rx,0,x,0,ry,floorY,0,0,1};
            visual.glow->transform(stretch); pool->fill(visual.glow);
            visual.floorClip->reset();
            for (int c=0;c<columns;++c) {
                float edge=floorEdges[c];
                if (edge<view) visual.floorClip->appendRect(c*sw/columns,edge,sw/columns+0.1f,view-edge);
            }
            pool->opacity(255);
        }
        // Billboard decorations share depth ordering and reusable render slots.
        struct PlantProjection { float x, distance; bool well, water; };
        std::array<PlantProjection, (sizeof(plants)/sizeof(plants[0]))+(sizeof(wells)/sizeof(wells[0]))> projected{};
        for (size_t i=0;i<projected.size();++i) {
            constexpr size_t plantCount=sizeof(plants)/sizeof(plants[0]);
            bool well=i>=plantCount;
            int x=well ? wells[i-plantCount][0] : plants[i][0];
            int y=well ? wells[i-plantCount][1] : plants[i][1];
            float dx=x+0.5f-px, dy=y+0.5f-py;
            float distance=dx*cameraCos+dy*cameraSin;
            projected[i]={-dx*cameraSin+dy*cameraCos,distance,well,well && wells[i-plantCount][2]!=0};
        }
        std::sort(projected.begin(),projected.end(),[](const auto& a,const auto& b) { return a.distance>b.distance; });
        for (size_t i=0;i<projected.size();++i) {
            auto& visual=plantVisuals[i]; visual.root->opacity(0);
            auto plant=projected[i];
            if (plant.distance<0.15f || plant.distance>=maxDepth) continue;
            float scale=projection/plant.distance;
            float x=sw/2+plant.x*scale, radius=scale*(plant.well ? 10 : 7)/32;
            if (x+radius<0 || x-radius>sw) continue;
            int first=std::clamp(int(std::floor((x-radius)*columns/sw)),0,columns-1);
            int last=std::clamp(int(std::floor((x+radius)*columns/sw)),0,columns-1);
            bool visible=false;
            for (int c=first;c<=last;++c) { visible |= plant.distance<depth[c]; }
            if (!visible) continue;
            visual.clip->reset();
            for (int c=first;c<=last;++c) {
                if (plant.distance<depth[c]) visual.clip->appendRect(c*sw/columns,0,sw/columns+0.1f,view);
            }
            float floorY=view/2+projection*0.5f/plant.distance;
            Matrix transform={scale,0,x,0,scale,floorY,0,0,1};
            visual.art->opacity(plant.well ? 0 : 255);
            visual.wellArt->opacity(plant.well ? 255 : 0);
            if (plant.well) {
                for (int i=0;i<8;++i) {
                    int dark=20+i*5;
                    visual.wellColors[16+i]->fill(plant.water ? 19+i*8 : dark,
                                                 plant.water ? 24+i*10 : dark+2,
                                                 plant.water ? 94+i*18 : dark);
                }
                visual.wellArt->transform(transform);
            }
            else visual.art->transform(transform);
            visual.root->opacity(255);
        }
        // Pixel silhouette follows the reference: raised sight, stepped slide,
        // dark grip and two warm hands. Each pixel remains a ThorVG rectangle.
        float cx=sw/2, base=view;
        constexpr const char* pistol[] = {
            "........K.......",
            ".......DMD......",
            "......DMLMD.....",
            "......DMLMD.....",
            "......DMLMD.....",
            ".....MMLLMM.....",
            "....DMDDDDMD....",
            "....DMLLLMMD....",
            "....DMLMGMMD....",
            "....DMLGGMMD....",
            "....DMLLLMMD....",
            "....DDMLMMDD....",
            "....KDGGGDKK....",
            "....KDGGGDKKR...",
            "...O.KDGGDKKR...",
            "...OOKDGGDKKRO..",
            "..OTTKDDDDKKRO..",
            "..THTKKKKKKKRO..",
            "..THTDKKKKKRROO.",
            "..THTTBRRRROOOO.",
            ".OTHTTTTTTTOORO.",
            ".OTHTHTTTTTTOOO.",
            ".OTTHHHTTTTTTOO.",
            ".OTTTTTTTTTTTOO."
        };
        struct PixelColor { char code; uint8_t r, g, b; };
        constexpr PixelColor palette[] = {
            {'K',8,9,8}, {'D',44,46,44}, {'G',71,74,71},
            {'M',115,118,113}, {'L',157,160,153},
            {'R',152,82,32}, {'B',103,55,24}, {'O',216,131,58},
            {'T',250,173,102}, {'H',255,205,152}
        };
        float pixel=sh*0.32f/24;
        float gunTop=base-24*pixel;
        if (resized) {
            size_t colorIndex=0;
            for (auto color : palette) {
                auto shape=gunShapes[colorIndex++]; shape->reset();
                shape->fill(color.r,color.g,color.b);
                for (int y=0; y<24; ++y) {
                    for (int x=0; x<16; ++x) {
                        if (pistol[y][x]==color.code) {
                            shape->appendRect(cx+(x-8)*pixel,gunTop+y*pixel,pixel+0.1f,pixel+0.1f);
                        }
                    }
                }
            }
        }
        Matrix recoil={1,0,0,0,1,shot>0 ? 2*pixel : 0,0,0,1};
        for (auto shape : gunShapes) { shape->transform(recoil); }
        muzzle->reset();
        if (shot>0) {
            float flashTop=gunTop+2*pixel;
            muzzle->appendRect(cx-2*pixel,flashTop-3*pixel,5*pixel,2*pixel);
            muzzle->appendRect(cx,flashTop-5*pixel,pixel,5*pixel);
        }
        // Crosshair.
        overlay->appendRect(cx-6,view/2,12,1);
        overlay->appendRect(cx,view/2-6,1,12);
        mapScene->opacity(minimap ? 255 : 0);
        float scale=std::min(sw*0.20f/W,sh*0.84f*0.42f/H), ox=16, oy=16;
        auto mapLine=[&](Shape* shape,float x1,float y1,float x2,float y2) {
            shape->moveTo(ox+x1*scale,oy+y1*scale);
            shape->lineTo(ox+x2*scale,oy+y2*scale);
        };
        if (resized) {
            // Keep only boundaries adjoining walkable cells, rather than filled tiles.
            mapWalls->reset();
            for (int y=0;y<H;++y) {
                for (int x=0;x<W;++x) {
                    if (!wall(x,y)) continue;
                    if (!wall(x-1,y)) mapLine(mapWalls,x,y,x,y+1);
                    if (!wall(x+1,y)) mapLine(mapWalls,x+1,y,x+1,y+1);
                    if (!wall(x,y-1)) mapLine(mapWalls,x,y,x+1,y);
                    if (!wall(x,y+1)) mapLine(mapWalls,x,y+1,x+1,y+1);
                }
            }
            mapDoorsDirty=true;
        }
        if (mapDoorsDirty) {
            mapDoors->reset();
            for (auto& d : doors) {
                if (d.open>=doorMaxOpen) continue;
                if (d.plane==0) mapLine(mapDoors,d.x+0.5f,d.y,d.x+0.5f,d.y+1);
                else mapLine(mapDoors,d.x,d.y+0.5f,d.x+1,d.y+0.5f);
            }
            mapDoorsDirty=false;
        }
        mapPlayer->reset();
        if (minimap) {
            mapPlayer->appendCircle(ox+px*scale,oy+py*scale,scale*1.08f,scale*1.08f);
            mapPlayer->moveTo(ox+px*scale,oy+py*scale);
            mapPlayer->lineTo(ox+(px+cameraCos*0.8f)*scale,oy+(py+cameraSin*0.8f)*scale);
            mapPlayer->lineTo(ox+(px+std::cos(angle+0.4f)*0.45f)*scale,oy+(py+std::sin(angle+0.4f)*0.45f)*scale);
            mapPlayer->close();
        }
    }
};
}

int main()
{
    if (tvg::Initializer::init() != tvg::Result::Success) return 1;
    if (tvg::toolkit::run(new Wolf, tvg::toolkit::RenderEngine::GL) != tvg::Result::Success) return 1;
    if (tvg::Initializer::term() != tvg::Result::Success) return 1;
}
