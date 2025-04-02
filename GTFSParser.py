import csv

# Build stop_id to stop_code dictionary
stop_id_to_code = {}
with open('stops.txt', mode='r', encoding='utf-8-sig') as stops_file:
    reader = csv.DictReader(stops_file)
    for row in reader:
        stop_id_to_code[row['stop_id']] = row['stop_code']

# Build trip_id to route_id and route_short_name mapping from trips.txt and routes.txt
trip_id_to_route_info = {}
route_id_to_short_name = {}

with open('routes.txt', mode='r', encoding='utf-8-sig') as routes_file:
    reader = csv.DictReader(routes_file)
    for row in reader:
        route_id_to_short_name[row['route_id']] = row['route_short_name']

with open('trips.txt', mode='r', encoding='utf-8-sig') as trips_file:
    reader = csv.DictReader(trips_file)
    for row in reader:
        route_short_name = route_id_to_short_name.get(row['route_id'])
        if route_short_name:
            trip_id_to_route_info[row['trip_id']] = {
                'route_id': row['route_id'],
                'route_code': route_short_name
            }

# Write stop_times_filtered.txt with desired columns
with open('stop_times.txt', mode='r', encoding='utf-8-sig') as stop_times_file, \
     open('stop_times_filtered.txt', mode='w', encoding='utf-8-sig', newline='') as output_file:

    reader = csv.DictReader(stop_times_file)
    fieldnames = ['route_code', 'route_id', 'arrival_time', 'stop_code']
    writer = csv.DictWriter(output_file, fieldnames=fieldnames)
    writer.writeheader()

    for i, row in enumerate(reader):
        stop_code = stop_id_to_code.get(row['stop_id'])
        route_info = trip_id_to_route_info.get(row['trip_id'])
        if stop_code and route_info:
            writer.writerow({
                'route_code': route_info['route_code'],
                'route_id': route_info['route_id'],
                'arrival_time': row['arrival_time'],
                'stop_code': stop_code
            })
        if i % 100000 == 0:
            print(f"Processed {i:,} lines...")