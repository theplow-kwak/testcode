#include <iostream>
#include <coroutine>

class Generator
{
public:
    struct promise_type
    {
        int value;

        std::suspend_always await_transform(int value)
        {
            this->value = value;
            return {};
        }

        Generator get_return_object()
        {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        auto initial_suspend() { return std::suspend_always{}; }

        auto return_void() { return; }

        auto final_suspend() noexcept { return std::suspend_always{}; }

        void unhandled_exception() { std::exit(1); }
    };

    std::coroutine_handle<promise_type> co_handler;

    Generator(std::coroutine_handle<promise_type> handler) : co_handler(handler) {}

    int next()
    {
        co_handler.resume();
        return co_handler.promise().value;
    }

    ~Generator()
    {
        if (true == (bool)co_handler)
        {
            co_handler.destroy();
        }
    }
};

Generator foo()
{
    int i = 0;
    while (i < 5)
        co_await i++;
}

int main()
{
    Generator task = foo();
    for (int i = 0; i < 15; i++)
        std::cout << "main " << i << " - " << task.next() << std::endl;
}
