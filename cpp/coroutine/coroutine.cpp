#include <iostream>
#include <coroutine>

class Task
{
public:
    struct promise_type
    {
        Task get_return_object()
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() { return std::suspend_always{}; }

        auto return_void() { return; }

        auto final_suspend() noexcept { return std::suspend_always{}; }

        void unhandled_exception() { std::exit(1); }
    };

    std::coroutine_handle<promise_type> co_handler;

    Task(std::coroutine_handle<promise_type> handler) : co_handler(handler) {}

    ~Task()
    {
        if (true == (bool)co_handler)
        {
            co_handler.destroy();
        }
    }
};

Task foo()
{
    std::cout << "foo 1" << std::endl;
    co_await std::suspend_always{};
    std::cout << "foo 2" << std::endl;
}

int main()
{
    Task task = foo();
    std::cout << "\t main 1" << std::endl;

    task.co_handler.resume(); 
    std::cout << "\t main 2" << std::endl;
    task.co_handler.resume(); 
}
