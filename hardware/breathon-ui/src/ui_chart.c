#include "ui_internal.h"
#include <math.h>
#include <stdio.h>

/* Labelled UI fixtures only. No writes to sensor state, history or backend. */
static const int gas[3][8]={{80,95,122,103,138,115,114,126},{90,112,99,135,118,148,140,126},{100,90,118,105,135,120,145,126}};
static const int heat[3][8]={{180,225,210,280,250,305,290,326},{180,230,215,280,260,320,300,326},{240,205,265,230,290,275,330,326}};
static lv_point_precise_t points[2][43];
const char *ui_chart_correlation(int period)
{
    static char text[24];
    double sx=0,sy=0,sxx=0,syy=0,sxy=0;
    for(int i=0;i<8;++i){double x=gas[period][i],y=heat[period][i];sx+=x;sy+=y;sxx+=x*x;syy+=y*y;sxy+=x*y;}
    int r=(int)(100*(8*sxy-sx*sy)/sqrt((8*sxx-sx*sx)*(8*syy-sy*sy))+0.5);
    snprintf(text,sizeof(text),"r +%d.%02d",r/100,r%100); return text;
}
void ui_chart_lines(lv_obj_t *parent,int period,bool gas_only)
{
    int width=gas_only?216:196, height=gas_only?62:68;
    for(int grid=1;grid<4;++grid) {
        lv_obj_t *line=ui_panel(parent,0,grid*height/4,width,1,gas_only?UI_CYAN:UI_PALE);
        lv_obj_set_style_bg_opa(line,gas_only?LV_OPA_20:LV_OPA_COVER,0);
        lv_obj_remove_flag(line,LV_OBJ_FLAG_CLICKABLE);
    }
    for(int series=0;series<(gas_only?1:2);++series) {
        for(int i=0;i<43;++i) {
            int segment=i/6, step=i%6;
            if(segment==7) { segment=6; step=6; }
            const int *values=series?heat[period]:gas[period];
            /* Smoothstep interpolates without overshooting the fixture samples. */
            int blend=step*step*(18-2*step);
            int value=values[segment]+(values[segment+1]-values[segment])*blend/216;
            int normalized=series?(value-150)*100/210:(value-70)*100/90;
            points[series][i].x=4+i*(width-8)/42;
            points[series][i].y=height-6-normalized*(height-12)/100;
        }
        lv_obj_t *curve=lv_line_create(parent); lv_obj_remove_style_all(curve);
        lv_line_set_points(curve,points[series],43);
        lv_obj_set_style_line_color(curve,lv_color_hex(gas_only?UI_CYAN:series?UI_PLOT_HEAT:UI_PLOT_GAS),0);
        lv_obj_set_style_line_width(curve,3,0); lv_obj_set_style_line_rounded(curve,true,0);
        lv_obj_remove_flag(curve,LV_OBJ_FLAG_CLICKABLE);
    }
}
