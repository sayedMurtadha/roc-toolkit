/*
 * Copyright (c) 2020 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_pipeline/transcoder_source.h"
#include "roc_audio/resampler_map.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"

namespace roc {
namespace pipeline {

TranscoderSource::TranscoderSource(const TranscoderConfig& config,
                                   sndio::ISource& input_source,
                                   core::IPool& buffer_pool,
                                   core::IArena& arena)
    : frame_factory_(buffer_pool)
    , input_source_(input_source)
    , frame_reader_(NULL)
    , config_(config)
    , init_status_(status::NoStatus) {
    config_.deduce_defaults();

    audio::IFrameReader* frm_reader = &input_source_;

    if (config_.input_sample_spec.channel_set()
        != config_.output_sample_spec.channel_set()) {
        const audio::SampleSpec from_spec(config_.input_sample_spec.sample_rate(),
                                          audio::Sample_RawFormat,
                                          config_.input_sample_spec.channel_set());

        const audio::SampleSpec to_spec(config_.input_sample_spec.sample_rate(),
                                        audio::Sample_RawFormat,
                                        config_.output_sample_spec.channel_set());

        channel_mapper_reader_.reset(
            new (channel_mapper_reader_) audio::ChannelMapperReader(
                *frm_reader, frame_factory_, from_spec, to_spec));
        if ((init_status_ = channel_mapper_reader_->init_status()) != status::StatusOK) {
            return;
        }
        frm_reader = channel_mapper_reader_.get();
    }

    if (config_.input_sample_spec.sample_rate()
        != config_.output_sample_spec.sample_rate()) {
        const audio::SampleSpec from_spec(config_.input_sample_spec.sample_rate(),
                                          audio::Sample_RawFormat,
                                          config_.output_sample_spec.channel_set());

        const audio::SampleSpec to_spec(config_.output_sample_spec.sample_rate(),
                                        audio::Sample_RawFormat,
                                        config_.output_sample_spec.channel_set());

        resampler_.reset(audio::ResamplerMap::instance().new_resampler(
            arena, frame_factory_, config_.resampler, from_spec, to_spec));
        if (!resampler_) {
            init_status_ = status::StatusNoMem;
            return;
        }
        if ((init_status_ = resampler_->init_status()) != status::StatusOK) {
            return;
        }

        resampler_reader_.reset(new (resampler_reader_) audio::ResamplerReader(
            *frm_reader, *resampler_, from_spec, to_spec));
        if ((init_status_ = resampler_reader_->init_status()) != status::StatusOK) {
            return;
        }
        frm_reader = resampler_reader_.get();
    }

    if (config_.enable_profiling) {
        profiler_.reset(new (profiler_) audio::ProfilingReader(
            *frm_reader, arena, config_.output_sample_spec, config_.profiler));
        if ((init_status_ = profiler_->init_status()) != status::StatusOK) {
            return;
        }
        frm_reader = profiler_.get();
    }

    frame_reader_ = frm_reader;
    init_status_ = status::StatusOK;
}

status::StatusCode TranscoderSource::init_status() const {
    return init_status_;
}

sndio::ISink* TranscoderSource::to_sink() {
    return NULL;
}

sndio::ISource* TranscoderSource::to_source() {
    return this;
}

sndio::DeviceType TranscoderSource::type() const {
    return input_source_.type();
}

sndio::DeviceState TranscoderSource::state() const {
    return input_source_.state();
}

void TranscoderSource::pause() {
    input_source_.pause();
}

bool TranscoderSource::resume() {
    return input_source_.resume();
}

bool TranscoderSource::restart() {
    return input_source_.restart();
}

audio::SampleSpec TranscoderSource::sample_spec() const {
    return config_.output_sample_spec;
}

core::nanoseconds_t TranscoderSource::latency() const {
    return 0;
}

bool TranscoderSource::has_latency() const {
    return false;
}

bool TranscoderSource::has_clock() const {
    return input_source_.has_clock();
}

void TranscoderSource::reclock(core::nanoseconds_t timestamp) {
    input_source_.reclock(timestamp);
}

bool TranscoderSource::read(audio::Frame& frame) {
    roc_panic_if(init_status_ != status::StatusOK);

    return frame_reader_->read(frame);
}

} // namespace pipeline
} // namespace roc
