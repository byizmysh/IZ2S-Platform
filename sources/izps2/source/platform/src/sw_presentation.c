#include "sw_presentation.h"

static int valid_ratio(uint32_t numerator,uint32_t denominator)
{
    return (!numerator && !denominator) ||
           (numerator && denominator && numerator<=65535u && denominator<=65535u);
}

static uint64_t gcd(uint64_t a,uint64_t b)
{
    while(b) { const uint64_t remainder=a%b;a=b;b=remainder; }
    return a;
}

int ps2_sw_presentation_rgba(const Ps2SwMergeImage *source,
                           const Ps2SwMergeOutput *target,
                           const Ps2SwPresentationOptions *options,
                           Ps2SwPresentationInfo *info)
{
    Ps2SwMergeImage input;
    Ps2SwMergeOutput output;
    Ps2SwPresentationOptions configuration={0};
    Ps2SwPresentationInfo result={0};
    Ps2SwMergeParameters merge={0};
    uint64_t desired_n,desired_d,viewport_n,viewport_d,divisor;
    uint32_t picture_width,picture_height;
    size_t row_bytes;
    int status;
    if(info)*info=result;
    if(!source || !target)return PS2_PRESENTATION_ARGUMENT;
    input=*source;output=*target;
    if(options)configuration=*options;
    if(!input.width || !input.height || input.width>1024u || input.height>1024u ||
       !output.width || !output.height || output.width>1024u || output.height>1024u ||
       !valid_ratio(configuration.source_pixel_aspect_num,configuration.source_pixel_aspect_den) ||
       !valid_ratio(configuration.desired_aspect_num,configuration.desired_aspect_den) ||
       !valid_ratio(configuration.viewport_aspect_num,configuration.viewport_aspect_den))
        return PS2_PRESENTATION_ARGUMENT;
    row_bytes=(size_t)output.width*4u;
    if(output.pitch<row_bytes || (size_t)(output.height-1u)>(SIZE_MAX-row_bytes)/output.pitch)
        return PS2_PRESENTATION_STORAGE;
    result.required_output_bytes=(size_t)(output.height-1u)*output.pitch+row_bytes;
    if(configuration.desired_aspect_num) {
        desired_n=configuration.desired_aspect_num;
        desired_d=configuration.desired_aspect_den;
    } else {
        const uint32_t pixel_n=configuration.source_pixel_aspect_num?configuration.source_pixel_aspect_num:1u;
        const uint32_t pixel_d=configuration.source_pixel_aspect_den?configuration.source_pixel_aspect_den:1u;
        desired_n=(uint64_t)input.width*pixel_n;
        desired_d=(uint64_t)input.height*pixel_d;
    }
    divisor=gcd(desired_n,desired_d);desired_n/=divisor;desired_d/=divisor;
    viewport_n=configuration.viewport_aspect_num?configuration.viewport_aspect_num:output.width;
    viewport_d=configuration.viewport_aspect_den?configuration.viewport_aspect_den:output.height;
    divisor=gcd(viewport_n,viewport_d);viewport_n/=divisor;viewport_d/=divisor;
    result.desired_aspect_num=(uint32_t)desired_n;
    result.desired_aspect_den=(uint32_t)desired_d;
    result.viewport_aspect_num=(uint32_t)viewport_n;
    result.viewport_aspect_den=(uint32_t)viewport_d;
    picture_width=output.width;picture_height=output.height;
    /* Dimension/rational bounds above keep these exact products below 2^53;
     * no floating-point aspect comparisons or overflowing wide fractions. */
    if(desired_n*viewport_d<viewport_n*desired_d) {
        picture_width=(uint32_t)(((uint64_t)output.width*desired_n*viewport_d)/(desired_d*viewport_n));
        if(!picture_width)picture_width=1;
    } else if(desired_n*viewport_d>viewport_n*desired_d) {
        picture_height=(uint32_t)(((uint64_t)output.height*viewport_n*desired_d)/(viewport_d*desired_n));
        if(!picture_height)picture_height=1;
    }
    result.picture.left=(int32_t)((output.width-picture_width)/2u);
    result.picture.top=(int32_t)((output.height-picture_height)/2u);
    result.picture.right=result.picture.left+(int32_t)picture_width;
    result.picture.bottom=result.picture.top+(int32_t)picture_height;
    merge.background_rgb=configuration.border_rgb;
    merge.circuit[1].enabled=1;
    merge.circuit[1].image=input;
    merge.circuit[1].source=(Ps2SwMergeRect){0,0,(int32_t)input.width,(int32_t)input.height};
    merge.circuit[1].destination=result.picture;
    result.merge_result=ps2_sw_merge_rgba(&output,&merge);
    switch(result.merge_result) {
    case PS2_SW_MERGE_OK:status=PS2_PRESENTATION_OK;break;
    case PS2_SW_MERGE_STORAGE:status=PS2_PRESENTATION_STORAGE;break;
    case PS2_SW_MERGE_ALIAS:status=PS2_PRESENTATION_ALIAS;break;
    case PS2_SW_MERGE_INVALID:status=PS2_PRESENTATION_ARGUMENT;break;
    default:status=PS2_PRESENTATION_MERGE;break;
    }
    if(info)*info=result;
    return status;
}
