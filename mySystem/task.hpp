#ifndef TASK_HPP
#define TASK_HPP

#include <memory>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include "utils.hpp" // only for myassert()

#define __TASK_MSG_OUT_OF_STACK_MEM "Out of memory allocating %ld bytes for %s stack for task %s"

class Task
{
protected:
    volatile TaskHandle_t mHandle = nullptr;
    typedef void(Task::*MethodPtr)();
    static constexpr const char* kTag = "task";
    static constexpr const char* kMsgAlreadyHaveTask = "Already have task";
#ifndef FREERTOS_HAVE_CUST_ALLOC_TASK
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
#endif
    union {
        TaskFunction_t mFunc;
        MethodPtr mMethod; // just a placeholder type
    };
    void* mArg;
    bool doCreate(const char* name, bool usePsram, uint32_t stackSize, BaseType_t cpuCore,
        UBaseType_t prio, TaskFunction_t func)
    {
        assert(!mHandle);
        if (usePsram) {
#ifdef FREERTOS_TASK_HAVE_CUST_ALLOC
            auto stack = (StackType_t*)heap_caps_malloc(stackSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!stack) {
                ESP_LOGE(kTag, __TASK_MSG_OUT_OF_STACK_MEM, stackSize, "PSRAM", name);
                return false;
            }
            auto tcb = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            myassert(tcb);
            mHandle = xTaskCreateCustAllocPinnedToCore(func, name, stackSize, this, prio, stack, tcb, cpuCore,
                [](void* pStack, void* pTcb) {
                    heap_caps_free(pStack);
                    heap_caps_free(pTcb);
            });
#else
            if (!mPsramCtx) {
                mPsramCtx = PsramCtx::create(stackSize);
                if (!mPsramCtx) {
                    ESP_LOGE(kTag, __TASK_MSG_OUT_OF_STACK_MEM, stackSize, "PSRAM", name);
                    return false;
                }
            }
            mHandle = xTaskCreateStaticPinnedToCore(func, name, stackSize, this, prio,
                (StackType_t*)mPsramCtx->stack, &mPsramCtx->taskStruct, cpuCore);
#endif
        }
        else {
            auto ret = xTaskCreatePinnedToCore(func, name, stackSize, this, prio, (TaskHandle_t*)&mHandle, cpuCore);
            if (ret != pdPASS) {
                mHandle = nullptr;
                ESP_LOGE(kTag, __TASK_MSG_OUT_OF_STACK_MEM, stackSize, "internal", name);
                return false;
            }
        }
        assert(mHandle);
        return true;
    }
    void cleanupBeforeExit()
    {
#ifndef FREERTOS_TASK_HAVE_CUST_ALLOC
        if (mPsramCtx) {
            // TCB are cleanup in IDLE task, so give it some time
            TimerHandle_t timer = xTimerCreate("cleanup", pdMS_TO_TICKS(4000), pdFALSE, mPsramCtx,
                [](TimerHandle_t xTimer) {
                    auto ctx = (PsramCtx*)pvTimerGetTimerID(xTimer);
                    xTimerDelete(xTimer, portMAX_DELAY);
                    PsramCtx::destroy(ctx);
                    ESP_LOGW(kTag, "Freed task PSRAM stack and RCB");
            });
            mPsramCtx = nullptr;
            xTimerStart(timer, portMAX_DELAY);
        }
#endif
        mHandle = nullptr;
        vTaskDelete(nullptr);
    }
public:
    TaskHandle_t handle() const { return mHandle; }
    void waitToEnd()
    {
        if (!mHandle) {
            return;
        }
        int time = 0;
        do {
            vTaskDelay(1);
            if ((++time % 100) == 0) {
                ESP_LOGE(kTag, "Destructor: Timeout waiting for task to end");
            }
        } while(mHandle);
    }
    ~Task() {
        waitToEnd();
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
};

#endif // TASK_HPP
