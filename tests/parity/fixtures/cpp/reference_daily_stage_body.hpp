// Original method body used by the synthetic stage SDK.
{
    c_.workflowStep(name, "running");
    status(name);
    for (int attempt = 0; attempt < retries_; ++attempt) {
        QString error;
        bool navigational = false;
        try {
            run();
            c_.workflowStep(name, "completed");
            return true;
        } catch (const Cancelled &) {
            throw;
        } catch (const ExecutionFailure &) {
            c_.workflowStep(name, "failed", "输入或执行通道异常，已停止后续操作");
            throw;
        } catch (const NavigationFailure &e) {
            error = errorText(e);
            navigational = true;
        } catch (const std::exception &e) {
            error = errorText(e);
        }
        if (c_.info)
            c_.info(name, error);
        // Material/result uncertainty is never retried as an entire daily stage.
        if (!navigational || !retryNavigation || attempt + 1 >= retries_) {
            c_.workflowStep(name, "failed", error);
            failed_.append(name + "：" + error);
            if (c_.notify)
                c_.notify("每日任务步骤未完成", name + "：" + error, true);
            navigation_.ensureMain(180);
            return false;
        }
        navigation_.ensureMain(180);
    }
    return false;
}