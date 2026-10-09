// Windows integration test. Run against the installed service with the VPN
// disconnected and the UI closed. Creates no default routes and uses no VPN
// credentials. Port 9 is a dummy proxy; only TUN lifecycle is exercised.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkInterface>
#include <QRemoteObjectNode>
#include <QTextStream>
#include <QThread>
#include <cstdlib>
#include <functional>
#include <memory>
#include "ipc.h"
#include "rep_ipc_interface_replica.h"
#include "rep_ipc_process_interface_replica.h"

static int tunCount()
{
    int count = 0;
    for (const auto &iface : QNetworkInterface::allInterfaces())
        if (iface.humanReadableName().startsWith("tun2")) ++count;
    return count;
}

static bool waitUntil(const std::function<bool()> &predicate, int timeout = 7000)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
        if (predicate()) return true;
        QThread::msleep(50);
    } while (timer.elapsed() < timeout);
    return false;
}

struct Owner {
    QRemoteObjectNode node;
    std::unique_ptr<IpcProcessInterfaceReplica> process;
};

static std::unique_ptr<Owner> startTun(IpcInterfaceReplica *service)
{
    auto created = service->createPrivilegedProcess();
    if (!created.waitForFinished(5000) || created.returnValue() < 0) return {};
    auto owner = std::make_unique<Owner>();
    owner->node.connectToNode(QUrl("local:" + amnezia::getIpcProcessUrl(created.returnValue())));
    owner->process.reset(owner->node.acquire<IpcProcessInterfaceReplica>());
    if (!owner->process->waitForSource(5000)) return {};
    owner->process->setProgram(amnezia::PermittedProcess::Tun2Socks);
    owner->process->setArguments({"-device", "tun://tun2", "-proxy", "socks5://127.0.0.1:9"});
    owner->process->start();
    auto started = owner->process->waitForStarted(5000);
    if (!started.waitForFinished(7000) || !started.returnValue()) return {};
    QByteArray output;
    if (!waitUntil([&] {
            auto read = owner->process->readAllStandardError();
            if (read.waitForFinished(2000)) output += read.returnValue();
            return output.contains("[STACK]");
        })) return {};
    return owner;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (tunCount()) {
        QTextStream(stderr) << "Refusing to disturb an existing TUN. Close the VPN first." << Qt::endl;
        return 2;
    }
    QRemoteObjectNode node;
    node.connectToNode(QUrl("local:" + amnezia::getIpcServiceUrl()));
    std::unique_ptr<IpcInterfaceReplica> service(node.acquire<IpcInterfaceReplica>());
    if (!service->waitForSource(5000)) return 3;
    auto checkAddress = [&] {
        auto result = service->createTun("tun2", "10.33.0.2");
        return result.waitForFinished(15000) && result.returnValue();
    };
    for (int cycle = 1; cycle <= 3; ++cycle) {
        auto owner = startTun(service.get());
        if (!owner || tunCount() != 1 || !checkAddress()) return 10 + cycle;
        QElapsedTimer timer;
        timer.start();
        if (!checkAddress() || timer.elapsed() > 2000) return 20 + cycle;
        QTextStream(stdout) << "PASS cycle " << cycle << ": start + repeat IP assignment ("
                            << timer.elapsed() << " ms)" << Qt::endl;
        if (app.arguments().contains("--abrupt-exit")) {
            QTextStream(stdout) << "Exiting without Qt cleanup; service must release the TUN" << Qt::endl;
            std::_Exit(0);
        }
        if (cycle == 1) {
            // A new client must replace an orphan even before its old control
            // connection is closed. This exercises full-path process matching.
            auto replacement = startTun(service.get());
            if (!replacement || !waitUntil([] { return tunCount() == 1; }) || !checkAddress()) return 31;
            owner.reset();
            QCoreApplication::processEvents();
            if (tunCount() != 1) return 32;
            owner = std::move(replacement);
            QTextStream(stdout) << "PASS replacement: exactly one TUN remains" << Qt::endl;
        }
        owner.reset();
        if (!waitUntil([] { return tunCount() == 0; })) return 40 + cycle;
        QTextStream(stdout) << "PASS cycle " << cycle << ": owner disconnect removed TUN" << Qt::endl;
    }
    return 0;
}
