#include "screen_aspect.h"
#include "screen_aspect.inc"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void checkSurface(float width, float height) {
    ScreenViewport fit=screenCalculateViewport(width,height,1920,1080,ASPECT_MODE_FIT);
    ScreenViewport fill=screenCalculateViewport(width,height,1920,1080,ASPECT_MODE_FILL);
    assert(fit.originX>=0 && fit.originY>=0 && fit.width<=width+1 && fit.height<=height+1);
    assert(fill.originX<=0 && fill.originY<=0 && fill.width>=width-1 && fill.height>=height-1);
    assert(fit.scaleX==fit.scaleY && fill.scaleX==fill.scaleY);
    assert(fabsf((fill.originX+fill.width*.5f)-width*.5f)<=.5f);
    assert(fabsf((fill.originY+fill.height*.5f)-height*.5f)<=.5f);
    // Pixel and point calculations describe the same image, with rounding only.
    ScreenViewport points=screenAspectViewport(width/3,height/3,1920,1080,ASPECT_MODE_FILL);
    assert(fabsf(points.width*3-fill.width)<=2 && fabsf(points.height*3-fill.height)<=2);
}
int main(void) {
    checkSurface(2622,1206); // iPhone 17 Pro rectangular display size.
    checkSurface(2868,1320); checkSurface(2048,1536); checkSurface(2360,1640);
    checkSurface(1920,1080); checkSurface(3440,1440);
    ScreenViewport phone=screenAspectViewport(2622,1206,1920,1080,ASPECT_MODE_FIT);
    assert(phone.originX==239 && phone.originY==0 && phone.width==2144 && phone.height==1206);
    phone=screenAspectViewport(2622,1206,1920,1080,ASPECT_MODE_FILL);
    assert(phone.originX==0 && phone.width==2622 && phone.height==1475 && phone.originY==-135);
    ScreenViewport invalid=screenAspectViewport(NAN,1206,1920,1080,ASPECT_MODE_FILL);
    assert(!invalid.width && !invalid.height && isfinite(invalid.originX));
    puts("Display aspect: phone/tablet fit, centered fill crop, pixel/point scaling and invalid sizes passed");
}
