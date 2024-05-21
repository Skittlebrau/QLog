#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <QRegularExpression>
#include "HamlibRigDrv.h"
#include "core/debug.h"
#include "rig/macros.h"
#include "core/SerialPort.h"

#ifndef HAMLIB_FILPATHLEN
#define HAMLIB_FILPATHLEN FILPATHLEN
#endif

// macro introduced hamlib 4.6
#ifndef PTTPORT
#define PTTPORT(r) (&r->state.pttport)
#endif

#define MUTEXLOCKER     qCDebug(runtime) << "Waiting for Drv mutex"; \
                        QMutexLocker locker(&drvLock); \
                        qCDebug(runtime) << "Using Drv"

MODULE_IDENTIFICATION("qlog.rig.driver.hamlibdrv");
QList<QPair<int, QString>> HamlibRigDrv::getModelList()
{
    FCT_IDENTIFICATION;

    QList<QPair<int, QString>> ret;

    rig_load_all_backends();
    rig_list_foreach(addRig, &ret);

    return ret;
}

QList<QPair<QString, QString> > HamlibRigDrv::getPTTTypeList()
{
    FCT_IDENTIFICATION;

    QList<QPair<QString, QString>> ret;

    ret << QPair<QString, QString>("none", tr("None"))
        << QPair<QString, QString>("cat", tr("CAT"))
        << QPair<QString, QString>("dtr", tr("DTR"))
        << QPair<QString, QString>("rts", tr("RTS"));

    return ret;
}

ptt_type_t HamlibRigDrv::stringToHamlibPTT_Type(const QString &in_ptt)
{
    FCT_IDENTIFICATION;

    if ( in_ptt == "cat" )
        return RIG_PTT_RIG;
    else if ( in_ptt == "dtr" )
        return RIG_PTT_SERIAL_DTR;
    else if ( in_ptt == "rts" )
        return RIG_PTT_SERIAL_RTS;

    return RIG_PTT_NONE;
}

int HamlibRigDrv::addRig(const rig_caps *caps, void *data)
{
    QList<QPair<int, QString>> *list = static_cast<QList<QPair<int, QString>>*>(data);

    QString name = QString("%1 %2 (%3)").arg(caps->mfg_name,
                                             caps->model_name,
                                             caps->version);

    list->append(QPair<int, QString>(caps->rig_model, name));
    return -1;
}

RigCaps HamlibRigDrv::getCaps(int model)
{
    FCT_IDENTIFICATION;

    const struct rig_caps *caps = rig_get_caps(model);
    RigCaps ret;

    ret.isNetworkOnly = (model == RIG_MODEL_NETRIGCTL);
    ret.needPolling = true;

    if ( caps )
    {
        ret.canGetFreq = ( caps->get_freq );
        ret.canGetMode = ( caps->get_mode );
        ret.canGetVFO =  ( caps->get_vfo );
        ret.canGetPWR = (((caps->has_get_level) & (RIG_LEVEL_RFPOWER)) && caps->power2mW );
        ret.canGetRIT = ( caps->get_rit && ((caps->has_get_func) & (RIG_FUNC_RIT)) );
        ret.canGetXIT = ( caps->get_xit && ((caps->has_get_func) & (RIG_FUNC_XIT)) );
        ret.canGetKeySpeed = ( ((caps->has_get_level) & (RIG_LEVEL_KEYSPD)) );

        //currently only CAT PTT is supported
        ret.canGetPTT = ( caps->get_ptt && (caps->ptt_type == RIG_PTT_RIG || caps->ptt_type == RIG_PTT_RIG_MICDATA));
        ret.canSendMorse = ( caps->send_morse != nullptr );

        /* due to a hamlib issue #855 (https://github.com/Hamlib/Hamlib/issues/855)
         * the PWR will be disabled for 4.2.x and 4.3.x for NETRIG
         */
#if ( HAMLIBVERSION_MAJOR == 4 && ( HAMLIBVERSION_MINOR == 2 || HAMLIBVERSION_MINOR == 3 ) )
        if ( caps->rig_model == RIG_MODEL_NETRIGCTL )
            ret.canGetPWR = false;
#endif

        ret.serialDataBits = caps->serial_data_bits;
        ret.serialStopBits = caps->serial_stop_bits;
    }
    return ret;
}

HamlibRigDrv::HamlibRigDrv(const RigProfile &profile,
                           QObject *parent)
    : GenericRigDrv(profile, parent),
      rig(nullptr),
      forceSendState(false),
      currPTT(false),
      currFreq(Hz(0)),
      currPBWidth(Hz(0)),
      currModeId(RIG_MODE_NONE),
      currVFO(RIG_VFO_NONE),
      currPWR(0),
      currRIT(0.0),
      currXIT(0.0),
      keySpeed(0),
      morseOverCatSupported(false)
{
    FCT_IDENTIFICATION;

    rig_load_all_backends();
    rig = rig_init(rigProfile.model);

    if ( !rig )
    {
        // initialization failed
        qCDebug(runtime) << "Cannot allocate Rig structure";
        lastErrorText = tr("Initialization Error");
    }

    rig_set_debug(RIG_DEBUG_BUG);
}

HamlibRigDrv::~HamlibRigDrv()
{
    FCT_IDENTIFICATION;

    if ( !drvLock.tryLock(200) )
    {
        qCDebug(runtime) << "Waited too long";
        // better to make a memory leak
        return;
    }

    if ( rig )
    {
        rig_close(rig);
        rig_cleanup(rig);
        rig = nullptr;
    }
    drvLock.unlock();
}


bool HamlibRigDrv::open()
{
    FCT_IDENTIFICATION;

    MUTEXLOCKER;

    if ( !rig )
    {
        // initialization failed
        lastErrorText = tr("Initialization Error");
        qCDebug(runtime) << "Rig is not initialized";
        return false;
    }

    RigProfile::rigPortType portType = rigProfile.getPortType();

    if ( portType == RigProfile::NETWORK_ATTACHED )
    {
        //handling Network Radio
        const QString portString = rigProfile.hostname + ":" + QString::number(rigProfile.netport);
        strncpy(rig->state.rigport.pathname, portString.toLocal8Bit().constData(), HAMLIB_FILPATHLEN - 1);
    }
    else if ( portType == RigProfile::SERIAL_ATTACHED )
    {
        //handling Serial Port Radio
        strncpy(rig->state.rigport.pathname, rigProfile.portPath.toLocal8Bit().constData(), HAMLIB_FILPATHLEN - 1);
        rig->state.rigport.parm.serial.rate = rigProfile.baudrate;
        rig->state.rigport.parm.serial.data_bits = rigProfile.databits;
        rig->state.rigport.parm.serial.stop_bits = rigProfile.stopbits;
        rig->state.rigport.parm.serial.handshake = stringToHamlibFlowControl(rigProfile.flowcontrol);
        rig->state.rigport.parm.serial.parity = stringToHamlibParity(rigProfile.parity);
    }
    else
    {
        lastErrorText = tr("Unsupported Rig Driver");
        qCDebug(runtime) << "Rig Open Error" << lastErrorText;
        return false;
    }

    ptt_type_t pttType = stringToHamlibPTT_Type(rigProfile.pttType);

    qCDebug(runtime) << "Using PTT Type" << rigProfile.pttType;
    PTTPORT(rig)->type.ptt = pttType;

    if ( pttType == RIG_PTT_SERIAL_RTS
         || pttType == RIG_PTT_SERIAL_DTR )
    {
        qCDebug(runtime) << "Using PTT Port" << rigProfile.pttPortPath;
        strncpy(PTTPORT(rig)->pathname, rigProfile.pttPortPath.toLocal8Bit().constData(), HAMLIB_FILPATHLEN - 1);
    }

    int status = rig_open(rig);

    if ( status != RIG_OK )
    {
        lastErrorText = hamlibErrorString(status);
        qCDebug(runtime) << "Rig Open Error" << lastErrorText;
        return false;
    }
    else
    {
        qCDebug(runtime) << "Rig Open - OK";
    }

    opened = true;
    currRIT = MHz(rigProfile.ritOffset);
    currXIT = MHz(rigProfile.xitOffset);
    morseOverCatSupported = ( rig->caps->send_morse != nullptr );

    connect(&timer, &QTimer::timeout, this, &HamlibRigDrv::checkRigStateChange);
    timer.start(rigProfile.pollInterval);
    emit rigIsReady();
    return true;
}

bool HamlibRigDrv::isMorseOverCatSupported()
{
    FCT_IDENTIFICATION;

    MUTEXLOCKER;

    return morseOverCatSupported;
}

QStringList HamlibRigDrv::getAvailableModes()
{
    FCT_IDENTIFICATION;

    MUTEXLOCKER;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return QStringList();
    }

    rmode_t localRigModes = RIG_MODE_NONE;
    QStringList modeList;

    if ( rig->caps->rig_model == RIG_MODEL_NETRIGCTL )
    {
        /* Limit a set of modes for network rig */
        localRigModes = static_cast<rmode_t>(RIG_MODE_CW|RIG_MODE_SSB|RIG_MODE_FM|RIG_MODE_AM);
    }
    else if ( rig->state.mode_list != RIG_MODE_NONE )
    {
        localRigModes = static_cast<rmode_t>(rig->state.mode_list);
    }

    /* hamlib 3.x and 4.x are very different - workaround */
    for ( unsigned char i = 0; i < (sizeof(rmode_t)*8)-1; i++ )
    {
        /* hamlib 3.x and 4.x are very different - workaround */
        const char *ms = rig_strrmode(static_cast<rmode_t>(localRigModes & rig_idx2setting(i)));

        if (!ms || !ms[0])
        {
            continue;
        }
        qCDebug(runtime) << "Supported Mode :" << ms;

        modeList.append(QString(ms));
    }

    return modeList;
}

void HamlibRigDrv::setFrequency(double newFreq)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << newFreq;

    if ( !rigProfile.getFreqInfo )
        return;

    if ( newFreq == currFreq )
        return;

    MUTEXLOCKER;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    int status = rig_set_freq(rig, RIG_VFO_CURR, newFreq);

    if ( status != RIG_OK )
    {
        lastErrorText = hamlibErrorString(status);
        qCWarning(runtime) << "Set Freq error" << lastErrorText;
        emit errorOccured(tr("Set Frequency Error"),
                          lastErrorText);
    }
    commandSleep();
}

void HamlibRigDrv::setRawMode(const QString &rawMode)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << rawMode;

    if ( !rigProfile.getModeInfo )
        return;

    MUTEXLOCKER;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    __setMode(rig_parse_mode(rawMode.toLatin1()));
}

void HamlibRigDrv::setMode(const QString &mode, const QString &subMode)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << mode << subMode;

    setRawMode((subMode.isEmpty()) ? mode : subMode);
}

void HamlibRigDrv::__setMode(rmode_t newModeID)
{
    FCT_IDENTIFICATION;

    if ( newModeID != RIG_MODE_NONE
         && newModeID != currModeId )
    {
        int status = rig_set_mode(rig, RIG_VFO_CURR, newModeID, RIG_PASSBAND_NOCHANGE);

        if ( status != RIG_OK )
        {
            lastErrorText = hamlibErrorString(status);
            qCWarning(runtime) << "Set KeySpeed error" << lastErrorText;
            //emit errorOccured(lastErrorText);
        }
        commandSleep();
    }
}

void HamlibRigDrv::setPTT(bool newPTTState)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << newPTTState;

    if ( !rigProfile.getPTTInfo || PTTPORT(rig)->type.ptt == RIG_PTT_NONE )
        return;

    MUTEXLOCKER;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    int status = rig_set_ptt(rig, RIG_VFO_CURR, (newPTTState ? RIG_PTT_ON : RIG_PTT_OFF));

    if ( status != RIG_OK )
    {
        lastErrorText = hamlibErrorString(status);
        qCWarning(runtime) << "Set PTT error" << lastErrorText;
        emit errorOccured(tr("Set PTT Error"),
                          lastErrorText);
    }

    // wait a moment because Rigs are slow and they are not possible to set and get
    // mode so quickly
    commandSleep();
}

void HamlibRigDrv::setKeySpeed(qint16 wpm)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << wpm;

    if ( !rigProfile.getKeySpeed )
        return;

    MUTEXLOCKER;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    __setKeySpeed(wpm);
}

void HamlibRigDrv::syncKeySpeed(qint16 wpm)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << wpm;

    if ( !rigProfile.keySpeedSync )
        return;

    MUTEXLOCKER;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    __setKeySpeed(wpm);
}

void HamlibRigDrv::sendMorse(const QString &text)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << text;

    if ( text.isEmpty() )
        return;

    MUTEXLOCKER;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    int status = rig_send_morse(rig, RIG_VFO_CURR, text.toLocal8Bit().constData());

    if ( status != RIG_OK )
    {
        lastErrorText = hamlibErrorString(status);
        qCWarning(runtime) << "Cannot sent Morse" << lastErrorText;
        //emit errorOccured(lastErrorText);
    }
    commandSleep();
}

void HamlibRigDrv::stopMorse()
{
    FCT_IDENTIFICATION;

    MUTEXLOCKER;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

#if (HAMLIBVERSION_MAJOR >= 4)
    int status = rig_stop_morse(rig, RIG_VFO_CURR);

    if ( status != RIG_OK )
    {
        lastErrorText = hamlibErrorString(status);
        qCWarning(runtime) << "Cannot Stop Morse" << lastErrorText;
        //emit errorOccured(lastErrorText);
    }
#endif
    commandSleep();
}

void HamlibRigDrv::sendState()
{
    FCT_IDENTIFICATION;

    MUTEXLOCKER;

    forceSendState = true;
}

void HamlibRigDrv::stopTimers()
{
    FCT_IDENTIFICATION;

    timer.stop();
}

void HamlibRigDrv::sendDXSpot(const DxSpot &)
{
    FCT_IDENTIFICATION;

    // no action
}

void HamlibRigDrv::checkChanges()
{
    FCT_IDENTIFICATION;

    if ( !checkFreqChange() )
        return;

    checkPTTChange();

    if ( !checkModeChange() )
        return;

    checkVFOChange();
    checkPWRChange();
    checkRITChange();
    checkXITChange();
    checkKeySpeedChange();
}

void HamlibRigDrv::checkRigStateChange()
{
    FCT_IDENTIFICATION;

    if ( !drvLock.tryLock(200) )
    {
        qCDebug(runtime) << "Waited too long";
        return;
    }

    qCDebug(runtime) << "Getting Rig state";

    checkChanges();

    forceSendState = false;

    // restart timer
    timer.start(rigProfile.pollInterval);
    drvLock.unlock();
}

void HamlibRigDrv::checkPTTChange()
{
    FCT_IDENTIFICATION;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    if ( rigProfile.getPTTInfo
         && rig->caps->get_ptt
         && ( rig->caps->ptt_type == RIG_PTT_RIG
              || rig->caps->ptt_type == RIG_PTT_RIG_MICDATA)
       )
    {
        ptt_t pttHamlib;
        int status = rig_get_ptt(rig, RIG_VFO_CURR, &pttHamlib);

        if ( status == RIG_OK )
        {
            bool ptt = ( pttHamlib == RIG_PTT_OFF ) ? false : true;

            qCDebug(runtime) << "Rig PTT:"<< ptt;
            qCDebug(runtime) << "Object PTT:"<< currPTT;

            if ( ptt != currPTT || forceSendState )
            {
                currPTT = ptt;
                qCDebug(runtime) << "emitting PTT changed" << currPTT;
                emit pttChanged(currPTT);
            }
        }
        else
        {
            lastErrorText = hamlibErrorString(status);
            qCWarning(runtime) << "Get PTT error" << lastErrorText;
        }
    }
    else
    {
        qCDebug(runtime) << "Get PTT is disabled";
    }
}

bool HamlibRigDrv::checkFreqChange()
{
    FCT_IDENTIFICATION;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return false;
    }

    if ( rigProfile.getFreqInfo
         && rig->caps->get_freq )
    {
        freq_t vfo_freq;
        int status = rig_get_freq(rig, RIG_VFO_CURR, &vfo_freq);

        if ( status == RIG_OK )
        {
            qCDebug(runtime) << "Rig Freq: "<< QSTRING_FREQ(Hz2MHz(vfo_freq));
            qCDebug(runtime) << "Object Freq: "<< QSTRING_FREQ(Hz2MHz(currFreq));

            if ( vfo_freq != currFreq || forceSendState )
            {
                currFreq = vfo_freq;
                qCDebug(runtime) << "emitting FREQ changed";
                emit frequencyChanged(Hz2MHz(currFreq),
                                      Hz2MHz(getRITFreq()),
                                      Hz2MHz(getXITFreq()));
            }
        }
        else
        {
            lastErrorText = hamlibErrorString(status);
            emit errorOccured(tr("Get Frequency Error"),
                              lastErrorText);
            qCWarning(runtime) << "Get Freq error" << lastErrorText;
            return false;
        }
    }
    else
    {
        qCDebug(runtime) << "Get Freq is disabled";
    }
    return true;
}

bool HamlibRigDrv::checkModeChange()
{
    FCT_IDENTIFICATION;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return false;
    }

    if ( rigProfile.getModeInfo
         && rig->caps->get_mode )
    {
        pbwidth_t pbwidth;
        rmode_t curr_modeId;

        int status = rig_get_mode(rig, RIG_VFO_CURR, &curr_modeId, &pbwidth);

        if ( status == RIG_OK )
        {
            qCDebug(runtime) << "Rig Mode: "<< curr_modeId << "Rig Filter: "<< pbwidth;
            qCDebug(runtime) << "Object Mode: "<< currModeId << "Object Filter:" << currPBWidth;

            if ( curr_modeId != currModeId
                 || ( pbwidth != RIG_PASSBAND_NOCHANGE && pbwidth != currPBWidth )
                 || forceSendState )
            {
                // mode change
                currModeId = curr_modeId;
                currPBWidth = pbwidth;

                QString submode;
                const QString mode = getModeNormalizedText(currModeId, submode);
                const QString &rawModeText = hamlibMode2String(currModeId);

                qCDebug(runtime) << "emitting MODE changed" << rawModeText << mode << submode << currPBWidth;
                emit modeChanged(rawModeText,
                                 mode, submode,
                                 currPBWidth);
            }
        }
        else
        {
            lastErrorText = hamlibErrorString(status);
            emit errorOccured(tr("Get Mode Error"),
                              lastErrorText);
            qCWarning(runtime) << "Get Mode error" << lastErrorText;
            return false;
        }
    }
    else
    {
        qCDebug(runtime) << "Get Mode is disabled";
    }
    return true;
}

void HamlibRigDrv::checkVFOChange()
{
    FCT_IDENTIFICATION;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    if ( rigProfile.getVFOInfo
         && rig->caps->get_vfo )
    {
        vfo_t curr_vfo;

        int status = rig_get_vfo(rig, &curr_vfo);

        if ( status == RIG_OK )
        {
            qCDebug(runtime) << "Rig VFO: "<< curr_vfo;
            qCDebug(runtime) << "Object VFO: "<< currVFO;

            if ( curr_vfo != currVFO || forceSendState )
            {
                currVFO = curr_vfo;
                const QString rawVFOText = hamlibVFO2String(currVFO);

                qCDebug(runtime) << "emitting VFO changed" << rawVFOText;
                emit vfoChanged(rawVFOText);
            }
        }
        else
        {
            lastErrorText = hamlibErrorString(status);
            qCWarning(runtime) << "Get VFO error" << lastErrorText;
        }
    }
    else
    {
        qCDebug(runtime) << "Get VFO is disabled";
    }
}

void HamlibRigDrv::checkPWRChange()
{
    FCT_IDENTIFICATION;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    if ( rigProfile.getPWRInfo
         && rig_has_get_level(rig, RIG_LEVEL_RFPOWER)
         && rig->caps->power2mW )
    {
        value_t rigPowerLevel;
        unsigned int rigPower;

        int status = rig_get_level(rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, &rigPowerLevel);

        if ( status == RIG_OK )
        {
            status = rig_power2mW(rig, &rigPower, rigPowerLevel.f, currFreq, currModeId);

            if (  status == RIG_OK )
            {
                qCDebug(runtime) << "Rig PWR: "<< rigPower;
                qCDebug(runtime) << "Object PWR: "<< currPWR;

                if ( rigPower != currPWR || forceSendState )
                {
                    currPWR = rigPower;

                    qCDebug(runtime) << "emitting PWR changed " << mW2W(currPWR);
                    emit powerChanged(mW2W(currPWR));
                }
            }
            else
            {
                lastErrorText = hamlibErrorString(status);
                qCWarning(runtime) << "Get PWR power2mw error" << lastErrorText;
            }
        }
        else
        {
            lastErrorText = hamlibErrorString(status);
            qCWarning(runtime) << "Get PWR error" << lastErrorText;
        }
    }
    else
    {
        qCDebug(runtime) << "Get PWR is disabled";
    }
}

void HamlibRigDrv::checkRITChange()
{
    FCT_IDENTIFICATION;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    if ( rigProfile.getRITInfo
         && rig->caps->get_rit
         && rig_has_get_func(rig, RIG_FUNC_RIT) )
    {
        int ritStatus;
        shortfreq_t rit = s_Hz(0);

        int status = rig_get_func(rig, RIG_VFO_CURR, RIG_FUNC_RIT, &ritStatus);

        if (  status == RIG_OK )
        {
            if ( ritStatus )
            {
                /* RIT is on */
                status = rig_get_rit(rig, RIG_VFO_CURR, &rit);
                if ( status != RIG_OK )
                {
                    rit = s_Hz(0);
                    lastErrorText = hamlibErrorString(status);
                    qCWarning(runtime) << "Get RIT error" << lastErrorText;
                }
            }
            else
            {
                /* RIT is off */
                rit = s_Hz(0);
            }

            qCDebug(runtime) << "Rig RIT:"<< rit << "Rig RIT Status:" << ritStatus;
            qCDebug(runtime) << "Object RIT: "<< currRIT;

            if ( static_cast<double>(rit) != currRIT || forceSendState )
            {
                currRIT = static_cast<double>(rit);

                qCDebug(runtime) << "emitting RIT changed" << QSTRING_FREQ(Hz2MHz(currRIT));
                qCDebug(runtime) << "emitting FREQ changed " << QSTRING_FREQ(Hz2MHz(currFreq))
                                                             << QSTRING_FREQ(Hz2MHz(getRITFreq()))
                                                             << QSTRING_FREQ(Hz2MHz(getXITFreq()));

                emit ritChanged(Hz2MHz(currRIT));
                emit frequencyChanged(Hz2MHz(currFreq),
                                      Hz2MHz(getRITFreq()),
                                      Hz2MHz(getXITFreq()));
            }
        }
        else
        {
            lastErrorText = hamlibErrorString(status);
            qCWarning(runtime) << "Get RIT Function error" << lastErrorText;
        }
    }
    else
    {
        qCDebug(runtime) << "Get RIT is disabled";
    }
}

void HamlibRigDrv::checkXITChange()
{
    FCT_IDENTIFICATION;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    if ( rigProfile.getXITInfo
         && rig->caps->get_xit
         && rig_has_get_func(rig, RIG_FUNC_XIT) )
    {
        int xitStatus;
        shortfreq_t xit = s_Hz(0);

        int status = rig_get_func(rig, RIG_VFO_CURR, RIG_FUNC_XIT, &xitStatus);

        if ( status == RIG_OK )
        {
            if ( xitStatus )
            {
                /* XIT is on */
                status = rig_get_xit(rig, RIG_VFO_CURR, &xit);
                if ( status != RIG_OK )
                {
                    xit = s_Hz(0);
                    lastErrorText = hamlibErrorString(status);
                    qCWarning(runtime) << "Get XIT error" << lastErrorText;
                }
            }
            else
            {
                /* XIT is off */
                xit = s_Hz(0);
            }

            qCDebug(runtime) << "RIG XIT: "<< xit << "Rig XIT Status:" << xitStatus;;
            qCDebug(runtime) << "Object XIT: "<< currXIT;

            if ( static_cast<double>(xit) != currXIT || forceSendState )
            {
                currXIT = static_cast<double>(xit);

                qCDebug(runtime) << "emitting XIT changed" << QSTRING_FREQ(Hz2MHz(currXIT));
                qCDebug(runtime) << "emitting FREQ changed " << QSTRING_FREQ(Hz2MHz(currFreq))
                                                             << QSTRING_FREQ(Hz2MHz(getRITFreq()))
                                                             << QSTRING_FREQ(Hz2MHz(getXITFreq()));

                emit xitChanged(Hz2MHz(currXIT));
                emit frequencyChanged(Hz2MHz(currFreq),
                                      Hz2MHz(getRITFreq()),
                                      Hz2MHz(getXITFreq()));
            }
        }
        else
        {
            lastErrorText = hamlibErrorString(status);
            qCWarning(runtime) << "Get XIT Function error" << lastErrorText;
        }
    }
    else
    {
        qCDebug(runtime) << "Get XIT is disabled";
    }
}

void HamlibRigDrv::checkKeySpeedChange()
{
    FCT_IDENTIFICATION;

    if ( !rig )
    {
        qCWarning(runtime) << "Rig is not active";
        return;
    }

    if ( rigProfile.getKeySpeed
         && rig_has_get_level(rig, RIG_LEVEL_KEYSPD) )
    {
        value_t rigKeySpeed;

        int status = rig_get_level(rig, RIG_VFO_CURR, RIG_LEVEL_KEYSPD, &rigKeySpeed);

        if ( status == RIG_OK )
        {
            qCDebug(runtime) << "RIG Key Speed: "<< rigKeySpeed.i;
            qCDebug(runtime) << "Object Key Speed: "<< keySpeed;

            if ( static_cast<unsigned int>(rigKeySpeed.i) != keySpeed || forceSendState )
            {
                keySpeed = static_cast<unsigned int>(rigKeySpeed.i);
                emit keySpeedChanged(keySpeed);
            }
        }
        else
        {
            lastErrorText = hamlibErrorString(status);
            qCWarning(runtime) << "Get KeySpeed error" << lastErrorText;
        }
    }
    else
    {
        qCDebug(runtime) << "Get KeySpeed is disabled";
    }
}

double HamlibRigDrv::getRITFreq()
{
    FCT_IDENTIFICATION;

    return currFreq + currRIT;
}

void HamlibRigDrv::setRITFreq(double rit)
{
    currRIT = rit;
}

double HamlibRigDrv::getXITFreq()
{
    return currFreq + currXIT;
}

void HamlibRigDrv::setXITFreq(double xit)
{
    currXIT = xit;
}

void HamlibRigDrv::__setKeySpeed(qint16 wpm)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << wpm;

    if ( wpm < 0 )
    {
        return;
    }

    value_t hamlibWPM;
    hamlibWPM.i = wpm;
    int status = rig_set_level(rig, RIG_VFO_CURR, RIG_LEVEL_KEYSPD, hamlibWPM);

    if ( status != RIG_OK )
    {
        lastErrorText = hamlibErrorString(status);
        qCWarning(runtime) << "Set KeySpeed error" << lastErrorText;
        //emit errorOccured(lastErrorText);
    }

    commandSleep();
}

void HamlibRigDrv::commandSleep()
{
#ifdef Q_OS_WIN
        Sleep(100);
#else
        usleep(100000);
#endif
}

const QString HamlibRigDrv::getModeNormalizedText(const rmode_t mode,
                                                  QString &submode) const
{
    submode = QString();

    switch ( mode )
    {
    case RIG_MODE_AM: return "AM";
    case RIG_MODE_CW: return "CW";
    case RIG_MODE_USB: {submode = "USB"; return "SSB";}
    case RIG_MODE_LSB: {submode = "LSB"; return "SSB";}
    case RIG_MODE_RTTY: return "RTTY";
    case RIG_MODE_FM: return "FM";
    case RIG_MODE_WFM: return "FM";
    case RIG_MODE_CWR: return "CW";
    case RIG_MODE_RTTYR: return "RTTY";
    case RIG_MODE_AMS: return "AM";
    case RIG_MODE_PKTLSB: {submode = "LSB"; return "SSB";}
    case RIG_MODE_PKTUSB: {submode = "USB"; return "SSB";}
    case RIG_MODE_PKTFM: return "FM";
    case RIG_MODE_ECSSUSB: {submode = "USB"; return "SSB";}
    case RIG_MODE_ECSSLSB: {submode = "LSB"; return "SSB";}
    case RIG_MODE_FAX: return "";
    case RIG_MODE_SAM: return "";
    case RIG_MODE_SAL: return "AM";
    case RIG_MODE_SAH: return "AM";
    case RIG_MODE_DSB: return "";
    case RIG_MODE_FMN: return "FM";
    case RIG_MODE_PKTAM: return "AM";
    default :
        return QString();
    }
}

const QString HamlibRigDrv::hamlibMode2String(const rmode_t mode) const
{
    const char *rawMode = rig_strrmode(mode);

    return QString(rawMode);
}

const QString HamlibRigDrv::hamlibVFO2String(const vfo_t vfo) const
{
    const char *rawVFO = rig_strvfo(vfo);
    return QString(rawVFO);
}

serial_handshake_e HamlibRigDrv::stringToHamlibFlowControl(const QString &in_flowcontrol)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << in_flowcontrol;

    const QString &flowcontrol = in_flowcontrol.toLower();

    if ( flowcontrol == SerialPort::SERIAL_FLOWCONTROL_SOFTWARE )
        return RIG_HANDSHAKE_XONXOFF;
    if ( flowcontrol == SerialPort::SERIAL_FLOWCONTROL_HARDWARE )
        return RIG_HANDSHAKE_HARDWARE;

    return RIG_HANDSHAKE_NONE;
}

serial_parity_e HamlibRigDrv::stringToHamlibParity(const QString &in_parity)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << in_parity;

    const QString &parity = in_parity.toLower();

    if ( parity == SerialPort::SERIAL_PARITY_EVEN )
        return RIG_PARITY_EVEN;
    if ( parity == SerialPort::SERIAL_PARITY_ODD )
        return RIG_PARITY_ODD;
    if ( parity == SerialPort::SERIAL_PARITY_MARK )
        return RIG_PARITY_MARK;
    if ( parity == SerialPort::SERIAL_PARITY_SPACE )
        return RIG_PARITY_SPACE;

    return RIG_PARITY_NONE;
}

QString HamlibRigDrv::hamlibErrorString(int errorCode)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << errorCode;

    static QRegularExpression re("[\r\n]");

    const QStringList &errorList = QString(rigerror(errorCode)).split(re);
    QString ret;

    if ( errorList.size() >= 1 )
    {
        ret = errorList.at(0);
    }

    qCDebug(runtime) << ret;

    return ret;
}

#undef MUTEXLOCKER
