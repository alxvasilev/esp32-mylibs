#ifndef TASK_HPP
#define TASK_HPP

#include <memory>
#include "mutex.hpp"

class Task
{
protected:
    TaskHandle_t mHandle = nullptr;
    typedef void(Task::*MethodPtr)();
    static constexpr const char* kTag = "task";
    static constexpr const char* kMsgAlreadyHaveTask = "Already have task";
    struct PsramCtx {
        void* stack;
        StaticTask_t taskStruct;
    protected:
        PsramCtx(uint32_t stackSize)
        : stack(heap_caps_malloc(stackSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) {}
        ~PsramCtx() { heap_caps_free(stack); }
    public:
        static PsramCtx* create(uint32_t stackSize)
        {
            void* mem = heap_caps_malloc(sizeof(PsramCtx), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            auto inst = new (mem) PsramCtx(stackSize);
            if (!inst->stack) {
                destroy(inst);
                return nullptr;
            }
            return inst;
        }
        static void destroy(PsramCtx* inst)
        {
            inst->~PsramCtx();
            heap_caps_free(inst);
        }
    };
    PsramCtx* mPsramCtx = nullptr;
    union {
        TaskFunction_t mFunc;
        MethodPtr mMethod; // just a placeholder type
    };
    void* mArg;
    bool destroyPsramCtx()
    {
        if (!mPsramCtx) {
            return false;
        }
        PsramCtx::destroy(mPsramCtx);
        mPsramCtx = nullptr;
        return true;
    }
    bool doCreate(const char* name, bool usePsram, uint32_t stackSize, BaseType_t cpuCore,
        UBaseType_t prio, TaskFunction_t func)
    {
        assert(!mHandle);
        if (usePsram) {
            if (!mPsramCtx) {
                mPsramCtx = PsramCtx::create(stackSize);
                if (!mPsramCtx) {
                    ESP_LOGE(kTag, "Out of memory allocating %lu PSRAM bytes stack for task %s", stackSize, name);
                    return false;
                }
            }
            mHandle = xTaskCreateStaticPinnedToCore(func, name, stackSize, this, prio,
                (StackType_t*)mPsramCtx->stack, &mPsramCtx->taskStruct, cpuCore);
        }
        else {
            destroyPsramCtx();
            auto ret = xTaskCreatePinnedToCore(func, name, stackSize, this, prio, &mHandle, cpuCore);
            if (ret != pdPASS) {
                mHandle = nullptr;
                ESP_LOGE(kTag, "Out of memory creating task %s with %lu bytes internal memory stack", name, stackSize);
                return false;
            }
        }
        assert(mHandle);
        return true;
    }
    void cleanupBeforeExit()
    {
        mHandle = nullptr;
        vTaskDelete(nullptr);
    }
public:
    TaskHandle_t handle() const { return mHandle; }
    ~Task() {
        if (mHandle) {
            ESP_LOGE(kTag, "Destroying a running task");
        }
        destroyPsramCtx();
    }
    template<class T>
    bool createTask(const char* name, bool usePsram, uint32_t stackSize, BaseType_t cpuCore,
        UBaseType_t prio, T* inst, void (T::*method)())
    {
        if (mHandle) {
            ESP_LOGW(kTag, "%s", kMsgAlreadyHaveTask);
            return false;
        }
        mMethod = reinterpret_cast<MethodPtr>(method);
        mArg = inst;
        return doCreate(name, usePsram, stackSize, cpuCore, prio, [](void* arg) {
            auto& self = *static_cast<Task*>(arg);
            (static_cast<T*>(self.mArg)->*((void(T::*)())self.mMethod))();
            self.cleanupBeforeExit();
        });
    }
    bool createTask(const char* name, bool usePsram, uint32_t stackSize, BaseType_t cpuCore,
        UBaseType_t prio, void* arg, TaskFunction_t func)
    {
        if (mHandle) {
            ESP_LOGW(kTag, "%s", kMsgAlreadyHaveTask);
            return false;
        }
        mFunc = func;
        mArg = arg;
        return doCreate(name, usePsram, stackSize, cpuCore, prio, [](void* aArg) {
            auto& self = *static_cast<Task*>(aArg);
            ((TaskFunction_t)self.mFunc)(self.mArg);
            self.cleanupBeforeExit();
        });
    }
    bool freePsramStack() {
        if (mHandle) {
            return false;
        }
        return destroyPsramCtx();
    }
};

#endif // TASK_HPP
