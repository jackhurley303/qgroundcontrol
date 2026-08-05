/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>

#include "qgc_plugin_api_global.h"

/// Versioned service id for QGCParameterDiffService. Breaking changes ship as
/// "qgc.parameterDiff/2", never as changes to this interface.
inline constexpr const char* QGCParameterDiffServiceId = "qgc.parameterDiff/1";

/// Keys of the result map returned by
/// QGCParameterDiffService::diffParametersFromFile().
namespace QGCParameterDiffResult {
inline constexpr const char* entries = "entries";                        ///< QVariantList of entry maps
inline constexpr const char* missingParameters = "missingParameters";    ///< QStringList
inline constexpr const char* invalidParameters = "invalidParameters";    ///< QStringList
inline constexpr const char* otherVehicle = "otherVehicle";              ///< bool
inline constexpr const char* multipleComponents = "multipleComponents";  ///< bool
inline constexpr const char* error = "error";                            ///< QString, empty on success
}  // namespace QGCParameterDiffResult

/// Keys of one entry inside QGCParameterDiffResult::entries.
namespace QGCParameterDiffEntry {
inline constexpr const char* componentId = "componentId";    ///< int
inline constexpr const char* name = "name";                  ///< QString
inline constexpr const char* fileValue = "fileValue";        ///< QString, display form of the file's value
inline constexpr const char* vehicleValue = "vehicleValue";  ///< QString, display form; empty when onVehicle is false
inline constexpr const char* onVehicle = "onVehicle";        ///< bool, false when the vehicle has no such parameter
inline constexpr const char* units = "units";                ///< QString, empty when onVehicle is false
inline constexpr const char* mavType = "mavType";            ///< int, a MAV_PARAM_TYPE a PARAM_SET can carry
inline constexpr const char* rawValue = "rawValue";          ///< QVariant, typed to mavType; what the write sends
}  // namespace QGCParameterDiffEntry

/**
 * @class QGCParameterDiffService
 * @ingroup PluginAPI
 * @brief Host service for diffing a parameter file against a vehicle and
 *        writing the differences back to it.
 *
 * Acquired via QGCHostServices::service(QGCParameterDiffServiceId) and cast
 * with qobject_cast. Vehicle ids are the system ids exposed by
 * QGCVehicleService (the vehicle object's "id" property).
 *
 * The two calls are a pair: diffParametersFromFile() reports what a file would
 * change, the caller presents that to the user, and writeParameterDiff() sends
 * back whatever subset the user accepted. Entries are opaque — pass them back
 * unmodified rather than rebuilding them.
 *
 * Both QGC's own tab-delimited format (VehicleId ComponentId Name Value Type)
 * and Mission Planner's two-column format (Name Value) are accepted. A file
 * naming parameters the vehicle does not have is not an error: in QGC format
 * they become entries with onVehicle false, typed by the file's own MAVLink
 * type; in Mission Planner format they are skipped and reported in
 * QGCParameterDiffResult::missingParameters, because that format carries no
 * type information to write them with.
 *
 * A file line the host cannot act on yields no entry at all and is reported in
 * QGCParameterDiffResult::invalidParameters — its value does not convert to the
 * parameter's type, or (off-vehicle only) its declared MAVLink type is not one
 * a PARAM_SET can carry. Every entry the diff does produce is therefore
 * writable as it stands.
 *
 * A vehicle's parameters must be *ready* (QGCParameterService::parametersReady)
 * before a diff means anything — until then the parameter cache is incomplete
 * and every file entry would look like a difference. Both calls refuse until
 * then.
 *
 * ABI: this vtable is frozen once the SDK ships — a new *virtual* goes to a
 * "qgc.parameterDiff/2" interface, never here. Additions that add no vtable
 * slot are fine in place: a new key in either namespace above, or a signal
 * (Qt's binary-compatibility rules allow appending one, and nothing outside the
 * host subclasses this). A write-completion/failure signal is the reserved
 * escape hatch here — do not burn a "/2" on it.
 */
class QGCPLUGINAPI_EXPORT QGCParameterDiffService : public QObject
{
    Q_OBJECT

public:
    explicit QGCParameterDiffService(QObject* parent = nullptr);
    ~QGCParameterDiffService() override;

    /// Compare the parameter file at filePath against the vehicle's current
    /// parameters. Reads the file synchronously — it may be deleted as soon as
    /// this returns.
    /// @return A result map keyed by QGCParameterDiffResult. On failure
    /// (unknown vehicle, parameters not ready, unreadable file, no parseable
    /// lines) "error" carries the reason and "entries" is empty.
    virtual QVariantMap diffParametersFromFile(int vehicleId, const QString& filePath) = 0;

    /// Write entries — the caller's chosen subset of a diffParametersFromFile()
    /// result's entries, unmodified — to the vehicle. An entry is skipped, not
    /// written, when its raw value no longer converts to the parameter's type,
    /// when the vehicle has since made the parameter read-only, or when it is
    /// malformed.
    ///
    /// The write is asynchronous: this call dispatches, and the host reports a
    /// write that the vehicle never acknowledges through its own user-facing
    /// message, as it does for any parameter write.
    ///
    /// @return The number of parameters actually sent to the vehicle — entries
    /// skipped for the reasons above, and entries whose value the vehicle
    /// already holds, are not counted. -1 when the vehicle is unknown or its
    /// parameters aren't ready.
    virtual int writeParameterDiff(int vehicleId, const QVariantList& entries) = 0;
};
