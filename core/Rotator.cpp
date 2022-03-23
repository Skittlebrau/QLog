#include <hamlib/rotator.h>
#include "Rotator.h"
#include "core/debug.h"
#include "data/RotProfile.h"

MODULE_IDENTIFICATION("qlog.core.rotator");

#ifndef HAMLIB_FILPATHLEN
#define HAMLIB_FILPATHLEN FILPATHLEN
#endif

static enum serial_handshake_e stringToFlowControl(const QString &in_flowcontrol)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters)<<in_flowcontrol;

    QString flowcontrol = in_flowcontrol.toLower();

    if ( flowcontrol == "software" ) return RIG_HANDSHAKE_XONXOFF;
    if ( flowcontrol == "hardware" ) return RIG_HANDSHAKE_HARDWARE;
    return RIG_HANDSHAKE_NONE;
}

static enum serial_parity_e stringToParity(const QString &in_parity)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters)<<in_parity;

    QString parity = in_parity.toLower();

    if ( parity == "even" ) return RIG_PARITY_EVEN;
    if ( parity == "odd" ) return RIG_PARITY_ODD;
    if ( parity == "mark" ) return RIG_PARITY_MARK;
    if ( parity == "space" ) return RIG_PARITY_SPACE;

    return RIG_PARITY_NONE;
}

Rotator::Rotator(QObject *parent) :
    QObject(parent),
    timer(nullptr)
{
    FCT_IDENTIFICATION;

    azimuth = 0;
    elevation = 0;
    rot = nullptr;
}

Rotator::~Rotator()
{
    if ( timer )
    {
        timer->stop();
        timer->deleteLater();
    }
}

Rotator* Rotator::instance() {
    FCT_IDENTIFICATION;

    static Rotator instance;
    return &instance;
}

void Rotator::start() {
    FCT_IDENTIFICATION;

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(1000);
}

void Rotator::update() {
    FCT_IDENTIFICATION;

    int status = RIG_OK;

    if (!rot) return;

    if (!rotLock.tryLock(200)) return;

    azimuth_t az;
    elevation_t el;

    status = rot_get_position(rot, &az, &el);
    if ( status == RIG_OK )
    {
        int newAzimuth = static_cast<int>(az);
        int newElevation = static_cast<int>(el);

        if (newAzimuth != this->azimuth || newElevation != this->elevation)  {
            this->azimuth = newAzimuth;
            this->elevation = newElevation;
            emit positionChanged(azimuth, elevation);
        }
    }
    else
    {
        __closeRot();
       emit rotErrorPresent(QString(tr("Get Position Error - ")) + QString(rigerror(status)));
    }

    timer->start(1000);
    rotLock.unlock();
}

void Rotator::open() {
    FCT_IDENTIFICATION;

    RotProfile rotProfile = RotProfilesManager::instance()->getCurProfile1();

    qCDebug(runtime) << "Opening profile name: " << rotProfile.profileName;

    rotLock.lock();

    // if rot is active then close it
    __closeRot();

    rot = rot_init(rotProfile.model);

    if ( !rot )
    {
        // initialization failed
        emit rotErrorPresent(QString(tr("Initialization Error")));
        rotLock.unlock();
        return;
    }
    if ( rot->caps->port_type == RIG_PORT_NETWORK
         || rot->caps->port_type == RIG_PORT_UDP_NETWORK )
    {
        // handling network rotator
        strncpy(rot->state.rotport.pathname, rotProfile.hostname.toLocal8Bit().constData(), HAMLIB_FILPATHLEN - 1);
        //port is hardcoded in hamlib - not necessary to set it.
    }
    else
    {
        // handling serial rotator
        strncpy(rot->state.rotport.pathname, rotProfile.portPath.toLocal8Bit().constData(), HAMLIB_FILPATHLEN - 1);
        rot->state.rotport.parm.serial.rate = rotProfile.baudrate;
        rot->state.rotport.parm.serial.data_bits = rotProfile.databits;
        rot->state.rotport.parm.serial.stop_bits = rotProfile.stopbits;
        rot->state.rotport.parm.serial.handshake = stringToFlowControl(rotProfile.flowcontrol);
        rot->state.rotport.parm.serial.parity = stringToParity(rotProfile.parity);
    }

    int status = rot_open(rot);

    if (status != RIG_OK)
    {
        __closeRot();
        emit rotErrorPresent(QString(tr("Open Connection Error - ")) + QString(rigerror(status)));
    }

    rotLock.unlock();
}

void Rotator::__closeRot()
{
    FCT_IDENTIFICATION;

    if (rot)
    {
        rot_close(rot);
        rot_cleanup(rot);
        rot = nullptr;
    }
}

void Rotator::close()
{
    FCT_IDENTIFICATION;

    rotLock.lock();
    __closeRot();
    rotLock.unlock();
}

void Rotator::setPosition(int azimuth, int elevation) {
    FCT_IDENTIFICATION;

    qCDebug(function_parameters)<<azimuth<< " " << elevation;

    if (!rot) return;
    rotLock.lock();
    int status = rot_set_position(rot, static_cast<azimuth_t>(azimuth), static_cast<elevation_t>(elevation));
    if (status != RIG_OK)
    {
        __closeRot();
        emit rotErrorPresent(QString(tr("Set Possition Error - ")) + QString(rigerror(status)));
    }

    rotLock.unlock();
}
