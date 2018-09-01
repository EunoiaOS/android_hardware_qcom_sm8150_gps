/* Copyright (c) 2011-2014, 2016-2018 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation, nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#define LOG_NDEBUG 0 //Define to enable LOGV
#define LOG_TAG "LocSvc_LocApiBase"

#include <dlfcn.h>
#include <inttypes.h>
#include <LocApiBase.h>
#include <LocAdapterBase.h>
#include <log_util.h>
#include <LocDualContext.h>

namespace loc_core {

#define TO_ALL_LOCADAPTERS(call) TO_ALL_ADAPTERS(mLocAdapters, (call))
#define TO_1ST_HANDLING_LOCADAPTERS(call) TO_1ST_HANDLING_ADAPTER(mLocAdapters, (call))

int hexcode(char *hexstring, int string_size,
            const char *data, int data_size)
{
   int i;
   for (i = 0; i < data_size; i++)
   {
      char ch = data[i];
      if (i*2 + 3 <= string_size)
      {
         snprintf(&hexstring[i*2], 3, "%02X", ch);
      }
      else {
         break;
      }
   }
   return i;
}

int decodeAddress(char *addr_string, int string_size,
                   const char *data, int data_size)
{
    const char addr_prefix = 0x91;
    int i, idxOutput = 0;

    if (!data || !addr_string) { return 0; }

    if (data[0] != addr_prefix)
    {
        LOC_LOGW("decodeAddress: address prefix is not 0x%x but 0x%x", addr_prefix, data[0]);
        addr_string[0] = '\0';
        return 0; // prefix not correct
    }

    for (i = 1; i < data_size; i++)
    {
        unsigned char ch = data[i], low = ch & 0x0F, hi = ch >> 4;
        if (low <= 9 && idxOutput < string_size - 1) { addr_string[idxOutput++] = low + '0'; }
        if (hi <= 9 && idxOutput < string_size - 1) { addr_string[idxOutput++] = hi + '0'; }
    }

    addr_string[idxOutput] = '\0'; // Terminates the string

    return idxOutput;
}

struct LocSsrMsg : public LocMsg {
    LocApiBase* mLocApi;
    inline LocSsrMsg(LocApiBase* locApi) :
        LocMsg(), mLocApi(locApi)
    {
        locallog();
    }
    inline virtual void proc() const {
        mLocApi->close();
        mLocApi->open(mLocApi->getEvtMask());
    }
    inline void locallog() const {
        LOC_LOGV("LocSsrMsg");
    }
    inline virtual void log() const {
        locallog();
    }
};

struct LocOpenMsg : public LocMsg {
    LocApiBase* mLocApi;
    inline LocOpenMsg(LocApiBase* locApi) :
            LocMsg(), mLocApi(locApi)
    {
        locallog();
    }
    inline virtual void proc() const {
        mLocApi->open(mLocApi->getEvtMask());
    }
    inline void locallog() const {
        LOC_LOGv("LocOpen Mask: %" PRIx64 "\n", mLocApi->getEvtMask());
    }
    inline virtual void log() const {
        locallog();
    }
};

struct LocCloseMsg : public LocMsg {
    LocApiBase* mLocApi;
    inline LocCloseMsg(LocApiBase* locApi) :
        LocMsg(), mLocApi(locApi)
    {
        locallog();
    }
    inline virtual void proc() const {
        mLocApi->close();
    }
    inline void locallog() const {
    }
    inline virtual void log() const {
        locallog();
    }
};

MsgTask* LocApiBase::mMsgTask;

LocApiBase::LocApiBase(LOC_API_ADAPTER_EVENT_MASK_T excludedMask,
                       ContextBase* context) :
    mContext(context),
    mMask(0), mExcludedMask(excludedMask)
{
    memset(mLocAdapters, 0, sizeof(mLocAdapters));

    if (nullptr == mMsgTask) {
        mMsgTask = new MsgTask("LocApiMsgTask", false);
    }
}

LOC_API_ADAPTER_EVENT_MASK_T LocApiBase::getEvtMask()
{
    LOC_API_ADAPTER_EVENT_MASK_T mask = 0;

    TO_ALL_LOCADAPTERS(mask |= mLocAdapters[i]->getEvtMask());

    return mask & ~mExcludedMask;
}

bool LocApiBase::isMaster()
{
    bool isMaster = false;

    for (int i = 0;
            !isMaster && i < MAX_ADAPTERS && NULL != mLocAdapters[i];
            i++) {
        isMaster |= mLocAdapters[i]->isAdapterMaster();
    }
    return isMaster;
}

bool LocApiBase::isInSession()
{
    bool inSession = false;

    for (int i = 0;
         !inSession && i < MAX_ADAPTERS && NULL != mLocAdapters[i];
         i++) {
        inSession = mLocAdapters[i]->isInSession();
    }

    return inSession;
}

void LocApiBase::addAdapter(LocAdapterBase* adapter)
{
    for (int i = 0; i < MAX_ADAPTERS && mLocAdapters[i] != adapter; i++) {
        if (mLocAdapters[i] == NULL) {
            mLocAdapters[i] = adapter;
            mMsgTask->sendMsg(new LocOpenMsg(this));
            break;
        }
    }
}

void LocApiBase::removeAdapter(LocAdapterBase* adapter)
{
    for (int i = 0;
         i < MAX_ADAPTERS && NULL != mLocAdapters[i];
         i++) {
        if (mLocAdapters[i] == adapter) {
            mLocAdapters[i] = NULL;

            // shift the rest of the adapters up so that the pointers
            // in the array do not have holes.  This should be more
            // performant, because the array maintenance is much much
            // less frequent than event handlings, which need to linear
            // search all the adapters
            int j = i;
            while (++i < MAX_ADAPTERS && mLocAdapters[i] != NULL);

            // i would be MAX_ADAPTERS or point to a NULL
            i--;
            // i now should point to a none NULL adapter within valid
            // range although i could be equal to j, but it won't hurt.
            // No need to check it, as it gains nothing.
            mLocAdapters[j] = mLocAdapters[i];
            // this makes sure that we exit the for loop
            mLocAdapters[i] = NULL;

            // if we have an empty list of adapters
            if (0 == i) {
                mMsgTask->sendMsg(new LocCloseMsg(this));
            } else {
                // else we need to remove the bit
                mMsgTask->sendMsg(new LocOpenMsg(this));
            }
        }
    }
}

void LocApiBase::updateEvtMask()
{
    mMsgTask->sendMsg(new LocOpenMsg(this));
}

void LocApiBase::handleEngineUpEvent()
{
    LocDualContext::injectFeatureConfig(mContext);

    // loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(mLocAdapters[i]->handleEngineUpEvent());
}

void LocApiBase::handleEngineDownEvent()
{    // This will take care of renegotiating the loc handle
    sendMsg(new LocSsrMsg(this));

    // loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(mLocAdapters[i]->handleEngineDownEvent());
}

void LocApiBase::reportPosition(UlpLocation& location,
                                GpsLocationExtended& locationExtended,
                                enum loc_sess_status status,
                                LocPosTechMask loc_technology_mask)
{
    // print the location info before delivering
    LOC_LOGD("flags: %d\n  source: %d\n  latitude: %f\n  longitude: %f\n  "
             "altitude: %f\n  speed: %f\n  bearing: %f\n  accuracy: %f\n  "
             "timestamp: %" PRId64 "\n"
             "Session status: %d\n Technology mask: %u\n "
             "SV used in fix (gps/glo/bds/gal/qzss) : \
             (0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 ")",
             location.gpsLocation.flags, location.position_source,
             location.gpsLocation.latitude, location.gpsLocation.longitude,
             location.gpsLocation.altitude, location.gpsLocation.speed,
             location.gpsLocation.bearing, location.gpsLocation.accuracy,
             location.gpsLocation.timestamp, status, loc_technology_mask,
             locationExtended.gnss_sv_used_ids.gps_sv_used_ids_mask,
             locationExtended.gnss_sv_used_ids.glo_sv_used_ids_mask,
             locationExtended.gnss_sv_used_ids.bds_sv_used_ids_mask,
             locationExtended.gnss_sv_used_ids.gal_sv_used_ids_mask,
             locationExtended.gnss_sv_used_ids.qzss_sv_used_ids_mask);
    // loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(
        mLocAdapters[i]->reportPositionEvent(location, locationExtended,
                                             status, loc_technology_mask)
    );
}

void LocApiBase::reportWwanZppFix(LocGpsLocation &zppLoc)
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(mLocAdapters[i]->reportWwanZppFix(zppLoc));
}

void LocApiBase::reportZppBestAvailableFix(LocGpsLocation &zppLoc,
        GpsLocationExtended &location_extended, LocPosTechMask tech_mask)
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(mLocAdapters[i]->reportZppBestAvailableFix(zppLoc,
            location_extended, tech_mask));
}

void LocApiBase::requestOdcpi(OdcpiRequestInfo& request)
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(mLocAdapters[i]->requestOdcpiEvent(request));
}

void LocApiBase::reportSv(GnssSvNotification& svNotify)
{
    const char* constellationString[] = { "Unknown", "GPS", "SBAS", "GLONASS",
        "QZSS", "BEIDOU", "GALILEO" };

    // print the SV info before delivering
    LOC_LOGV("num sv: %zu\n"
        "      sv: constellation svid         cN0"
        "    elevation    azimuth    flags",
        svNotify.count);
    for (size_t i = 0; i < svNotify.count && i < LOC_GNSS_MAX_SVS; i++) {
        if (svNotify.gnssSvs[i].type >
            sizeof(constellationString) / sizeof(constellationString[0]) - 1) {
            svNotify.gnssSvs[i].type = GNSS_SV_TYPE_UNKNOWN;
        }
        LOC_LOGV("   %03zu: %*s  %02d    %f    %f    %f    %f    0x%02X",
            i,
            13,
            constellationString[svNotify.gnssSvs[i].type],
            svNotify.gnssSvs[i].svId,
            svNotify.gnssSvs[i].cN0Dbhz,
            svNotify.gnssSvs[i].elevation,
            svNotify.gnssSvs[i].azimuth,
            svNotify.gnssSvs[i].carrierFrequencyHz,
            svNotify.gnssSvs[i].gnssSvOptionsMask);
    }
    // loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(
        mLocAdapters[i]->reportSvEvent(svNotify)
        );
}

void LocApiBase::reportSvMeasurement(GnssSvMeasurementSet &svMeasurementSet)
{
    // loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(
        mLocAdapters[i]->reportSvMeasurementEvent(svMeasurementSet)
    );
}

void LocApiBase::reportSvPolynomial(GnssSvPolynomial &svPolynomial)
{
    // loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(
        mLocAdapters[i]->reportSvPolynomialEvent(svPolynomial)
    );
}

void LocApiBase::reportStatus(LocGpsStatusValue status)
{
    // loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(mLocAdapters[i]->reportStatus(status));
}

void LocApiBase::reportNmea(const char* nmea, int length)
{
    // loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(mLocAdapters[i]->reportNmeaEvent(nmea, length));
}

void LocApiBase::reportXtraServer(const char* url1, const char* url2,
                                  const char* url3, const int maxlength)
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(mLocAdapters[i]->reportXtraServer(url1, url2, url3, maxlength));

}

void LocApiBase::requestXtraData()
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(mLocAdapters[i]->requestXtraData());
}

void LocApiBase::requestTime()
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(mLocAdapters[i]->requestTime());
}

void LocApiBase::requestLocation()
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(mLocAdapters[i]->requestLocation());
}

void LocApiBase::requestATL(int connHandle, LocAGpsType agps_type,
                            LocApnTypeMask apn_type_mask)
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(
            mLocAdapters[i]->requestATL(connHandle, agps_type, apn_type_mask));
}

void LocApiBase::releaseATL(int connHandle)
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(mLocAdapters[i]->releaseATL(connHandle));
}

void LocApiBase::requestNiNotify(GnssNiNotification &notify, const void* data)
{
    // loop through adapters, and deliver to the first handling adapter.
    TO_1ST_HANDLING_LOCADAPTERS(mLocAdapters[i]->requestNiNotifyEvent(notify, data));
}

void* LocApiBase :: getSibling()
    DEFAULT_IMPL(NULL)

LocApiProxyBase* LocApiBase :: getLocApiProxy()
    DEFAULT_IMPL(NULL)

void LocApiBase::reportGnssMeasurementData(GnssMeasurementsNotification& measurements,
                                           int msInWeek)
{
    // loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(mLocAdapters[i]->reportGnssMeasurementDataEvent(measurements, msInWeek));
}

void LocApiBase::reportGnssSvIdConfig(const GnssSvIdConfig& config)
{
    // Print the config
    LOC_LOGv("gloBlacklistSvMask: %" PRIu64 ", bdsBlacklistSvMask: %" PRIu64 ",\n"
             "qzssBlacklistSvMask: %" PRIu64 ", galBlacklistSvMask: %" PRIu64,
             config.gloBlacklistSvMask, config.bdsBlacklistSvMask,
             config.qzssBlacklistSvMask, config.galBlacklistSvMask);

    // Loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(mLocAdapters[i]->reportGnssSvIdConfigEvent(config));
}

void LocApiBase::reportGnssSvTypeConfig(const GnssSvTypeConfig& config)
{
    // Print the config
    LOC_LOGv("blacklistedMask: %" PRIu64 ", enabledMask: %" PRIu64,
             config.blacklistedSvTypesMask, config.enabledSvTypesMask);

    // Loop through adapters, and deliver to all adapters.
    TO_ALL_LOCADAPTERS(mLocAdapters[i]->reportGnssSvTypeConfigEvent(config));
}

enum loc_api_adapter_err LocApiBase::
   open(LOC_API_ADAPTER_EVENT_MASK_T /*mask*/)
DEFAULT_IMPL(LOC_API_ADAPTER_ERR_SUCCESS)

enum loc_api_adapter_err LocApiBase::
    close()
DEFAULT_IMPL(LOC_API_ADAPTER_ERR_SUCCESS)

void LocApiBase::startFix(const LocPosMode& /*posMode*/, LocApiResponse* /*adapterResponse*/)
DEFAULT_IMPL()

void LocApiBase::stopFix(LocApiResponse* /*adapterResponse*/)
DEFAULT_IMPL()

void LocApiBase::
    deleteAidingData(const GnssAidingData& /*data*/, LocApiResponse* /*adapterResponse*/)
DEFAULT_IMPL()

void LocApiBase::
    injectPosition(double /*latitude*/, double /*longitude*/, float /*accuracy*/)
DEFAULT_IMPL()

void LocApiBase::
    injectPosition(const Location& /*location*/, bool /*onDemandCpi*/)
DEFAULT_IMPL()

void LocApiBase::
    setTime(LocGpsUtcTime /*time*/, int64_t /*timeReference*/, int /*uncertainty*/)
DEFAULT_IMPL()

enum loc_api_adapter_err LocApiBase::
    setXtraData(char* /*data*/, int /*length*/)
DEFAULT_IMPL(LOC_API_ADAPTER_ERR_SUCCESS)

void LocApiBase::
   atlOpenStatus(int /*handle*/, int /*is_succ*/, char* /*apn*/, uint32_t /*apnLen*/,
                 AGpsBearerType /*bear*/, LocAGpsType /*agpsType*/,
                 LocApnTypeMask /*mask*/)
DEFAULT_IMPL()

void LocApiBase::
    atlCloseStatus(int /*handle*/, int /*is_succ*/)
DEFAULT_IMPL()

void LocApiBase::
    setPositionMode(const LocPosMode& /*posMode*/)
DEFAULT_IMPL()

LocationError LocApiBase::
    setServerSync(const char* /*url*/, int /*len*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

LocationError LocApiBase::
    setServerSync(unsigned int /*ip*/, int /*port*/, LocServerType /*type*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

void LocApiBase::
    informNiResponse(GnssNiResponse /*userResponse*/, const void* /*passThroughData*/)
DEFAULT_IMPL()

LocationError LocApiBase::
    setSUPLVersionSync(GnssConfigSuplVersion /*version*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

enum loc_api_adapter_err LocApiBase::
    setNMEATypesSync (uint32_t /*typesMask*/)
DEFAULT_IMPL(LOC_API_ADAPTER_ERR_SUCCESS)

LocationError LocApiBase::
    setLPPConfigSync(GnssConfigLppProfile /*profile*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

enum loc_api_adapter_err LocApiBase::
    setSensorControlConfigSync(int /*sensorUsage*/,
                           int /*sensorProvider*/)
DEFAULT_IMPL(LOC_API_ADAPTER_ERR_SUCCESS)

enum loc_api_adapter_err LocApiBase::
    setSensorPropertiesSync(bool /*gyroBiasVarianceRandomWalk_valid*/,
                        float /*gyroBiasVarianceRandomWalk*/,
                        bool /*accelBiasVarianceRandomWalk_valid*/,
                        float /*accelBiasVarianceRandomWalk*/,
                        bool /*angleBiasVarianceRandomWalk_valid*/,
                        float /*angleBiasVarianceRandomWalk*/,
                        bool /*rateBiasVarianceRandomWalk_valid*/,
                        float /*rateBiasVarianceRandomWalk*/,
                        bool /*velocityBiasVarianceRandomWalk_valid*/,
                        float /*velocityBiasVarianceRandomWalk*/)
DEFAULT_IMPL(LOC_API_ADAPTER_ERR_SUCCESS)

enum loc_api_adapter_err LocApiBase::
    setSensorPerfControlConfigSync(int /*controlMode*/,
                               int /*accelSamplesPerBatch*/,
                               int /*accelBatchesPerSec*/,
                               int /*gyroSamplesPerBatch*/,
                               int /*gyroBatchesPerSec*/,
                               int /*accelSamplesPerBatchHigh*/,
                               int /*accelBatchesPerSecHigh*/,
                               int /*gyroSamplesPerBatchHigh*/,
                               int /*gyroBatchesPerSecHigh*/,
                               int /*algorithmConfig*/)
DEFAULT_IMPL(LOC_API_ADAPTER_ERR_SUCCESS)

LocationError LocApiBase::
    setAGLONASSProtocolSync(GnssConfigAGlonassPositionProtocolMask /*aGlonassProtocol*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

LocationError LocApiBase::
    setLPPeProtocolCpSync(GnssConfigLppeControlPlaneMask /*lppeCP*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

LocationError LocApiBase::
    setLPPeProtocolUpSync(GnssConfigLppeUserPlaneMask /*lppeUP*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

GnssConfigSuplVersion LocApiBase::convertSuplVersion(const uint32_t /*suplVersion*/)
DEFAULT_IMPL(GNSS_CONFIG_SUPL_VERSION_1_0_0)

GnssConfigLppProfile LocApiBase::convertLppProfile(const uint32_t /*lppProfile*/)
DEFAULT_IMPL(GNSS_CONFIG_LPP_PROFILE_RRLP_ON_LTE)

GnssConfigLppeControlPlaneMask LocApiBase::convertLppeCp(const uint32_t /*lppeControlPlaneMask*/)
DEFAULT_IMPL(0)

GnssConfigLppeUserPlaneMask LocApiBase::convertLppeUp(const uint32_t /*lppeUserPlaneMask*/)
DEFAULT_IMPL(0)

void LocApiBase::
   getWwanZppFix()
DEFAULT_IMPL()

void LocApiBase::
   getBestAvailableZppFix()
DEFAULT_IMPL()

LocationError LocApiBase::
    setGpsLockSync(GnssConfigGpsLock /*lock*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

void LocApiBase::
    installAGpsCert(const LocDerEncodedCertificate* /*pData*/,
                    size_t /*length*/,
                    uint32_t /*slotBitMask*/)
DEFAULT_IMPL()

LocationError LocApiBase::
    setXtraVersionCheckSync(uint32_t /*check*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

LocationError LocApiBase::setBlacklistSvSync(const GnssSvIdConfig& /*config*/)
DEFAULT_IMPL(LOCATION_ERROR_SUCCESS)

void LocApiBase::setBlacklistSv(const GnssSvIdConfig& /*config*/)
DEFAULT_IMPL()

void LocApiBase::getBlacklistSv()
DEFAULT_IMPL()

void LocApiBase::setConstellationControl(const GnssSvTypeConfig& /*config*/)
DEFAULT_IMPL()

void LocApiBase::getConstellationControl()
DEFAULT_IMPL()

void LocApiBase::resetConstellationControl()
DEFAULT_IMPL()

} // namespace loc_core
