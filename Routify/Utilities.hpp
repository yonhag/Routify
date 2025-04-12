#define _USE_MATH_DEFINES // For M_PI
#include <cmath>

// Define M_PI if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class Utilities {
public:
    static double calculateHaversineDistance(double lat1, double lon1, double lat2, double lon2) {
        const double R = 6371.0; // Earth radius in kilometers
        if (std::fabs(lat1 - lat2) < 1e-9 && std::fabs(lon1 - lon2) < 1e-9) { return 0.0; }
        double dLat = (lat2 - lat1) * M_PI / 180.0;
        double dLon = (lon2 - lon1) * M_PI / 180.0;
        lat1 = lat1 * M_PI / 180.0; lat2 = lat2 * M_PI / 180.0;
        double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1) * cos(lat2) * sin(dLon / 2) * sin(dLon / 2);
        if (a < 0.0) a = 0.0; if (a > 1.0) a = 1.0; // Clamp 'a' to [0, 1] range
        double c = 2 * atan2(sqrt(a), sqrt(1.0 - a));
        return R * c;
    }
};