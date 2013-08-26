/** Copyright (c) 2013, Jonathan Bohren, all rights reserved. 
 * This software is released under the BSD 3-clause license, for the details of
 * this license, please see LICENSE.txt at the root of this repository. 
 */

#ifndef __CONMAN_HOOK_H
#define __CONMAN_HOOK_H

#include <rtt/RTT.hpp>
#include <rtt/Service.hpp>
#include <rtt/Logger.hpp>
#include <rtt/plugin/PluginLoader.hpp>

#include <conman/conman.h>

namespace conman {

  /** \brief ServiceRequester for using the conman HookServiec in C++
   *
   * This ServiceRequester will connect to the conman::HookService of a given
   * RTT TaskContext for easier use in C++.
   *
   */
  class Hook : public RTT::ServiceRequester 
  {
  public:

    typedef boost::shared_ptr<Hook> Ptr;
    typedef boost::shared_ptr<const Hook> ConstPtr;

    Hook(RTT::TaskContext *owner) :
      RTT::ServiceRequester("conman_hook", owner),
      // :'<,'>s/      \(.\+\)/      \1("\1"),/g
      getPeriod("getPeriod"),
      setOutputLayer("setOutputLayer"),
      setInputExclusivity("setInputExclusivity"),
      getInputExclusivity("getInputExclusivity"),
      getOutputLayer("getOutputLayer"),
      getOutputPortsOnLayer("getOutputPortsOnLayer"),
      setReadHardwareHook("setReadHardwareHook"),
      setComputeEstimationHook("setComputeEstimationHook"),
      setComputeControlHook("setComputeControlHook"),
      setWriteHardwareHook("setWriteHardwareHook"),
      readHardware("readHardware"),
      computeEstimation("computeEstimation"),
      computeControl("computeControl"),
      writeHardware("writeHardware")
    { 
      this->addOperationCaller(getPeriod);
      this->addOperationCaller(setOutputLayer);
      this->addOperationCaller(setInputExclusivity);
      this->addOperationCaller(getInputExclusivity);
      this->addOperationCaller(getOutputLayer);
      this->addOperationCaller(getOutputPortsOnLayer);
      this->addOperationCaller(setReadHardwareHook);
      this->addOperationCaller(setComputeEstimationHook);
      this->addOperationCaller(setComputeControlHook);
      this->addOperationCaller(setWriteHardwareHook);
      this->addOperationCaller(readHardware);
      this->addOperationCaller(computeEstimation);
      this->addOperationCaller(computeControl);
      this->addOperationCaller(writeHardware);
    }
      
    ~Hook() { }

    RTT::OperationCaller<RTT::os::TimeService::Seconds(void)>
      getPeriod;
    RTT::OperationCaller<bool(const std::string&, const conman::Layer::ID)>
      setOutputLayer;
    RTT::OperationCaller<bool(const std::string&, const Exclusivity::Mode)>
      setInputExclusivity;
    RTT::OperationCaller<conman::Exclusivity::Mode(const std::string&)>
      getInputExclusivity;
    RTT::OperationCaller<conman::Layer::ID(const std::string&)>
      getOutputLayer;
    RTT::OperationCaller<void(const conman::Layer::ID, std::vector<RTT::base::PortInterface*>&)>
      getOutputPortsOnLayer;
    RTT::OperationCaller<bool(const std::string&)>
      setReadHardwareHook;
    RTT::OperationCaller<bool(const std::string&)>
      setComputeEstimationHook;
    RTT::OperationCaller<bool(const std::string&)>
      setComputeControlHook;
    RTT::OperationCaller<bool(const std::string&)>
      setWriteHardwareHook;
    RTT::OperationCaller<void(RTT::os::TimeService::Seconds, RTT::os::TimeService::Seconds)>
      readHardware;
    RTT::OperationCaller<void(RTT::os::TimeService::Seconds, RTT::os::TimeService::Seconds)>
      computeEstimation;
    RTT::OperationCaller<void(RTT::os::TimeService::Seconds, RTT::os::TimeService::Seconds)> 
      computeControl;
    RTT::OperationCaller<void(RTT::os::TimeService::Seconds, RTT::os::TimeService::Seconds)>
      writeHardware;
    
    //! Checks if an RTT task has the conman Hook RTT service
    static bool HasHook(RTT::TaskContext *task)
    {
      return task->provides()->hasService("conman_hook");
    }

    //! Get the conman Hook service and return 
    static boost::shared_ptr<Hook> GetHook(RTT::TaskContext *tc)
    {
      // Return the service
      return tc->getProvider<Hook>("conman_hook");
    }
  };

}

#endif // ifndef __CONMAN_HOOK_H
