#pragma once

#include "obspp-video.hpp"

namespace obs {

video::video(video_t *video)
 : handle(video)
{

}

video::~video()
{

}

int video::reset(struct obs_video_info *info)
{
    return obs_reset_video(info);
}

void video::output(int channel, input &source)
{
    obs_set_output_source(channel, source.dangerous());
}

input video::output(int channel)
{
    return obs_get_output_source(channel);
}

/* Video Encoder */
video_encoder::video_encoder(std::string id, std::string name)
 : handle(obs_video_encoder_create(id.c_str(), name.c_str(), NULL, NULL))
{

}

}