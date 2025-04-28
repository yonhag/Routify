#pragma once
#define _USE_MATH_DEFINES // For M_PI
#include <cmath> // For trig functions, sqrt, etc. used in Haversine

// Define M_PI if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Utilities {

    const double WALK_SPEED_KPH = 5.0;
    const double ASSUMED_PUBLIC_TRANSPORT_SPEED_KPH = 50.0;

    // --- Coordinates Struct ---
    struct Coordinates {
        double latitude = 0.0;
        double longitude = 0.0;

        // Default constructor
        Coordinates() = default;

        // Constructor with values
        Coordinates(double lat, double lon) : latitude(lat), longitude(lon) {}

        // Optional: Basic validity check
        bool isValid() const {
            return latitude >= -90.0 && latitude <= 90.0 &&
                longitude >= -180.0 && longitude <= 180.0;
        }

        // Optional: Equality operator
        bool operator==(const Coordinates& other) const = default;
    };
    // --- End Coordinates Struct ---

    // --- Haversine Function ---
    inline double calculateHaversineDistance(const Coordinates& coord1, const Coordinates& coord2) {
        const double R = 6371.0; // Earth radius in kilometers

        double lat1Rad = coord1.latitude * M_PI / 180.0;
        double lon1Rad = coord1.longitude * M_PI / 180.0;
        double lat2Rad = coord2.latitude * M_PI / 180.0;
        double lon2Rad = coord2.longitude * M_PI / 180.0;

        double dLat = lat2Rad - lat1Rad;
        double dLon = lon2Rad - lon1Rad;

        double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
            cos(lat1Rad) * cos(lat2Rad) *
            sin(dLon / 2.0) * sin(dLon / 2.0);
        double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

        return R * c; // Distance in kilometers
    }

} // namespace Utilities