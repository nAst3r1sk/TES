#ifndef POSITION_SUBSCRIPTION_MANAGER_H
#define POSITION_SUBSCRIPTION_MANAGER_H

namespace tes {
namespace execution {
    class ExecutionController;
}
}

// C接口声明，供main.cpp使用
extern "C" {
    /**
     * 初始化持仓订阅管理器
     * @param execution_controller ExecutionController实例指针
     * @return 成功返回true，失败返回false
     */
    bool initialize_position_subscription_manager(tes::execution::ExecutionController* execution_controller);
    
    /**
     * 启动持仓订阅管理器
     * @return 成功返回true，失败返回false
     */
    bool start_position_subscription_manager();
    
    /**
     * 停止持仓订阅管理器
     */
    void stop_position_subscription_manager();
    
    /**
     * 清理持仓订阅管理器资源
     */
    void cleanup_position_subscription_manager();
}

#endif // POSITION_SUBSCRIPTION_MANAGER_H