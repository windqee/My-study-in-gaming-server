#include "shared/contracts/messages.hpp"
#include "version.hpp"

#include <iostream>

int main() {
    using namespace netarena;
    const auto next = checked_increment(InputSeq{41});
    if (!next.ok()) {
        std::cerr << "foundation sequence demonstration failed\n";
        return 1;
    }
    SteadyClock clock;
    const auto start = clock.now();
    Envelope hello{1, "foundation-demo", std::nullopt, std::nullopt, std::nullopt,
                   Hello{1, "local-demo"}};
    const auto elapsed = std::chrono::duration_cast<Duration>(clock.now() - start);
    std::cout << "NetArena " << version << " foundations demonstration (no network server)\n"
              << "InputSeq=" << to_decimal(*next.value_if())
              << "; protocol=" << hello.protocol_version
              << "; Hello=" << (hello.kind() == MessageKind::Hello)
              << "; local elapsed_ns=" << elapsed.count() << '\n';
}
