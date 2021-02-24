#include <memory>

extern "C" {
#include "mtpng.h"
}

// RAII wrappers for mtpng objects

auto create_encoder_options()
{
    mtpng_encoder_options* options = nullptr;
    if (mtpng_encoder_options_new(&options) != MTPNG_RESULT_OK) {
        throw std::runtime_error("mtpng_encoder_options_new failed");
    }
    struct deleter {
        void operator()(mtpng_encoder_options* p) { mtpng_encoder_options_release(&p); }
    };
    return std::unique_ptr<mtpng_encoder_options, deleter>(options);
}

auto create_threadpool(const size_t nthreads = MTPNG_THREADS_DEFAULT)
{
    mtpng_threadpool* pool = nullptr;
    if (mtpng_threadpool_new(&pool, nthreads) != MTPNG_RESULT_OK) {
        throw std::runtime_error("mtpng_threadpool_new failed");
    }
    struct deleter {
        void operator()(mtpng_threadpool* p) { mtpng_threadpool_release(&p); }
    };
    return std::unique_ptr<mtpng_threadpool, deleter>(pool);
}

auto create_header()
{
    mtpng_header* header = nullptr;
    if (mtpng_header_new(&header) != MTPNG_RESULT_OK) {
        throw std::runtime_error("mtpng_header_new failed");
    }
    struct deleter {
        void operator()(mtpng_header* p) { mtpng_header_release(&p); }
    };
    return std::unique_ptr<mtpng_header, deleter>(header);
}

auto create_encoder(mtpng_write_func write_func, mtpng_flush_func flush_func,
                    void* data, mtpng_encoder_options* options)
{
    mtpng_encoder* encoder = nullptr;
    if (mtpng_encoder_new(&encoder, write_func, flush_func, data, options) != MTPNG_RESULT_OK) {
        throw std::runtime_error("mtpng_encoder_new failed");
    };
    struct deleter {
        void operator()(mtpng_encoder* p) { mtpng_encoder_finish(&p); }
    };
    return std::unique_ptr<mtpng_encoder, deleter>(encoder);
}
