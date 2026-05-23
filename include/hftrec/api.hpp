#pragma once

#if defined(_WIN32) && defined(HFTREC_SHARED)
#if defined(HFTREC_BUILDING_LIBRARY)
#define HFTREC_API __declspec(dllexport)
#else
#define HFTREC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && defined(HFTREC_SHARED)
#define HFTREC_API __attribute__((visibility("default")))
#else
#define HFTREC_API
#endif