#pragma once
#include <cstddef>
#include <cstdint>
namespace okww::rust {
enum class WorkflowOp : int32_t {
#define OKWW_WORKFLOW_OP(name, id) name = id,
#define OKWW_WORKFLOW_METHOD(name, id, count)
#include "workflow.def"
#undef OKWW_WORKFLOW_OP
#undef OKWW_WORKFLOW_METHOD
};
enum class WorkflowMethod : int32_t {
#define OKWW_WORKFLOW_OP(name, id)
#define OKWW_WORKFLOW_METHOD(name, id, count) name = id,
#include "workflow.def"
#undef OKWW_WORKFLOW_OP
#undef OKWW_WORKFLOW_METHOD
};
struct WorkflowHost {
    void *context;
    int32_t (*invoke)(void *, int32_t, const double *, size_t, double *);
};
static_assert(sizeof(WorkflowHost) == 16);
extern "C" int32_t okww_rs_workflow_run(const WorkflowHost *, int32_t, const double *, size_t, double *);
} // namespace okww::rust
