#include <cassert>
#include <boost/chrono.hpp>

#include "pcrincludes.h"
#include "ltc2444.h"
#include "adcconsumer.h"
#include "adccontroller.h"
#include "qpcrapplication.h"

using namespace std;

const LTC2444::OversamplingRatio kThermistorOversamplingRate = LTC2444::kOversamplingRatio512;
const LTC2444::OversamplingRatio kLIAOversamplingRate = LTC2444::kOversamplingRatio512;

////////////////////////////////////////////////////////////////////////////////
// Class ADCController
ADCController::ADCController(ConsumersList &&consumers, unsigned int csPinNumber, SPIPort &&spiPort, unsigned int busyPinNumber):
    _consumers(std::move(consumers)) {
    _currentConversionState = static_cast<ADCState>(0);
    _workState = false;

    _ltc2444 = new LTC2444(csPinNumber, std::move(spiPort), busyPinNumber);

}

ADCController::~ADCController() {
    stop();

    if (joinable())
        join();

    delete _ltc2444;
}

void ADCController::process() {
    setMaxRealtimePriority();

    _ltc2444->readSingleEndedChannel(4, kThermistorOversamplingRate); //start first read

    static const boost::chrono::nanoseconds repeatFrequencyInterval((boost::chrono::nanoseconds::rep)round(1.0 / kADCRepeatFrequency * 1000 * 1000 * 1000));
    boost::chrono::high_resolution_clock::time_point repeatFrequencyLastTime = boost::chrono::high_resolution_clock::now();

    try {
        _workState = true;

        while (_workState) {
            if (_ltc2444->waitBusy())
                continue;

            ADCState state = nextState();

            //ensure ADC loop runs at regular interval without jitter
            if (state == 0) {
                loopStarted();

                boost::chrono::high_resolution_clock::time_point previousTime = repeatFrequencyLastTime;
                repeatFrequencyLastTime = boost::chrono::high_resolution_clock::now();

                boost::chrono::nanoseconds executionTime = repeatFrequencyLastTime - previousTime;

                if (executionTime < repeatFrequencyInterval) {
                    timespec time;
                    time.tv_sec = 0;
                    time.tv_nsec = (repeatFrequencyInterval - executionTime).count();

                    nanosleep(&time, nullptr);

                    repeatFrequencyLastTime = boost::chrono::high_resolution_clock::now();
                }
                else
                    std::cout << "ADCController::process - ADC measurements could not be completed in scheduled time\n";
            }

            //schedule conversion for next state, retrieve previous conversion value
            uint32_t value;
            switch (state) {
            case EReadZone1Singular:
                value = _ltc2444->readSingleEndedChannel(0, kThermistorOversamplingRate);
                break;
            case EReadZone2Singular:
                value = _ltc2444->readSingleEndedChannel(1, kThermistorOversamplingRate);
                break;
            case EReadLIA:
                value = _ltc2444->readSingleEndedChannel(6, kLIAOversamplingRate);
                break;
            case EReadLid:
                value = _ltc2444->readSingleEndedChannel(7, kThermistorOversamplingRate);
                break;
            default:
                throw std::logic_error("ADCController::process - unknown adc state");
            }

            try {
                //process previous conversion value
                _consumers[_currentConversionState]->setADCValue(value);
            }
            catch (const TemperatureLimitError &ex) {
                qpcrApp.stopExperiment(ex.what());
            }

            _currentConversionState = state;
        }
    }
    catch (...) {
        qpcrApp.setException(std::current_exception());
    }
}

void ADCController::stop() {
    _workState = false;
    _ltc2444->stopWaitinigBusy();
}

ADCController::ADCState ADCController::nextState() const {
    ADCController::ADCState nextState = static_cast<ADCController::ADCState>(static_cast<int>(_currentConversionState) + 1);
    return nextState == EFinal ? static_cast<ADCController::ADCState>(0) : nextState;
}
