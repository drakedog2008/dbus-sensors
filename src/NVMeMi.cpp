#include "NVMeMi.hpp"

#include <cerrno>
#include <iostream>

nvme_root_t NVMeMi::nvmeRoot = nvme_mi_create_root(stderr, DEFAULT_LOGLEVEL);

NVMeMi::NVMeMi(boost::asio::io_context& io, sdbusplus::bus_t& dbus, int bus,
               int addr) :
    io(io),
    dbus(dbus)
{
    if (!nvmeRoot)
    {
        throw std::runtime_error("invalid NVMe root");
    }

    // init mctp ep via mctpd
    int i = 0;
    for (;; i++)
    {
        try
        {
            auto msg = dbus.new_method_call(
                "xyz.openbmc_project.MCTP", "/xyz/openbmc_project/mctp",
                "au.com.CodeConstruct.MCTP", "SetupEndpoint");

            msg.append("mctpi2c" + std::to_string(bus));
            msg.append(std::vector<uint8_t>{static_cast<uint8_t>(addr)});
            auto reply = msg.call(); // throw SdBusError

            reply.read(eid);
            reply.read(nid);
            reply.read(mctpPath);
            break;
        }
        catch (const std::exception& e)
        {
            if (i < 5)
            {
                std::cerr << "retry to SetupEndpoint: " << e.what()
                          << std::endl;
            }
            else
            {
                throw std::runtime_error(e.what());
            }
        }
    }

    // open mctp endpoint
    nvmeEP = nvme_mi_open_mctp(nvmeRoot, nid, eid);
    if (!nvmeEP)
    {
        throw std::runtime_error("can't open MCTP endpoint " +
                                 std::to_string(nid) + ":" +
                                 std::to_string(eid));
    }

    // start worker thread
    workerStop = false;
    thread = std::thread([&io = workerIO, &stop = workerStop, &mtx = workerMtx,
                          &cv = workerCv]() {
        // With BOOST_ASIO_DISABLE_THREADS, boost::asio::executor_work_guard
        // issues null_event across the thread, which caused invalid invokation.
        // We implement a simple invoke machenism based std::condition_variable.
        while (1)
        {
            io.run();
            io.restart();
            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock);
                if (stop)
                {
                    // exhaust all tasks and exit
                    io.run();
                    break;
                }
            }
            
        }
    });
}

NVMeMi::~NVMeMi()
{
    // close worker
    workerStop = true;
    {
        std::unique_lock<std::mutex> lock(workerMtx);
        workerCv.notify_all();
    }
    thread.join();

    // close EP
    if (nvmeEP)
    {
        nvme_mi_close(nvmeEP);
    }

    // TODO: delete mctp ep from mctpd
}

void NVMeMi::post(std::function<void(void)>&& func)
{
    if (!workerStop)
    {
        std::unique_lock<std::mutex> lock(workerMtx);
        if (!workerStop)
        {
            workerIO.post(std::move(func));
            workerCv.notify_all();
            return;
        }
    }
    throw std::runtime_error("NVMeMi has been stopped");
}

void NVMeMi::miSubsystemHealthStatusPoll(
    std::function<void(const std::error_code&, nvme_mi_nvm_ss_health_status*)>&&
        cb)
{
    if (!nvmeEP)
    {
        std::cerr << "nvme endpoint is invalid" << std::endl;

        io.post([cb{std::move(cb)}]() {
            cb(std::make_error_code(std::errc::no_such_device), nullptr);
        });
        return;
    }

    try
    {
        post([self{shared_from_this()}, cb{std::move(cb)}]() {
            nvme_mi_nvm_ss_health_status ss_health;
            auto rc = nvme_mi_mi_subsystem_health_status_poll(self->nvmeEP,
                                                              true, &ss_health);
            if (rc)
            {

                std::cerr << "fail to subsystem_health_status_poll: "
                          << std::strerror(errno) << std::endl;
                self->io.post([cb{std::move(cb)}]() {
                    cb(std::make_error_code(static_cast<std::errc>(errno)),
                       nullptr);
                });
                return;
            }
            self->io.post(
                [cb{std::move(cb)}, ss_health{std::move(ss_health)}]() mutable {
                cb({}, &ss_health);
            });
        });
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << e.what() << std::endl;
        io.post([cb{std::move(cb)}]() {
            cb(std::make_error_code(std::errc::no_such_device), {});
        });
        return;
    }
}

void NVMeMi::miScanCtrl(std::function<void(const std::error_code&,
                                           const std::vector<nvme_mi_ctrl_t>&)>
                            cb)
{
    if (!nvmeEP)
    {
        std::cerr << "nvme endpoint is invalid" << std::endl;

        io.post([cb{std::move(cb)}]() {
            cb(std::make_error_code(std::errc::no_such_device), {});
        });
        return;
    }

    try
    {
        post([self{shared_from_this()}, cb{std::move(cb)}]() {
            int rc = nvme_mi_scan_ep(self->nvmeEP, true);
            if (rc)
            {
                std::cerr << "fail to scan controllers" << std::endl;
                self->io.post([cb{std::move(cb)}]() {
                    cb(std::make_error_code(std::errc::bad_message), {});
                });
                return;
            }

            std::vector<nvme_mi_ctrl_t> list;
            nvme_mi_ctrl_t c;
            nvme_mi_for_each_ctrl(self->nvmeEP, c)
            {
                list.push_back(c);
            }
            self->io.post(
                [cb{std::move(cb)}, list{std::move(list)}]() { cb({}, list); });
        });
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << e.what() << std::endl;
        io.post([cb{std::move(cb)}]() {
            cb(std::make_error_code(std::errc::no_such_device), {});
        });
        return;
    }
}