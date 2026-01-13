# BondClient

The `BondClient` class manages the lifecycle of a bond between the Fabric node and the capabilities server. Bonds are used to monitor the connection and ensure both sides are alive and responsive during plan execution.

## Purpose
- Establishes a heartbeat connection (bond) with the capabilities server for a specific plan execution (identified by a bond ID).

- Monitors the bond state to detect if the connection is formed or broken.

- Cleans up the bond when execution is complete or the client is destroyed.

## Main Methods

| Method         | Purpose                                                      |
|----------------|--------------------------------------------------------------|
| `BondClient(node, bond_id)` | Constructor. Initializes the bond client with the node and bond ID. Retrieves the bond topic from parameters. |
| `start()`      | Starts the bond, sets heartbeat period and timeout, and logs the event. |
| `stop()`       | Stops and cleans up the bond, logging the event.             |
| `~BondClient()`| Destructor. Ensures the bond is stopped and cleaned up.      |

## Callbacks
- `on_formed()`: Called when the bond is successfully established. Logs the event.

- `on_broken()`: Called when the bond is broken (e.g., server disconnects or crashes). Logs the event.

## Usage in Fabric
- Each plan execution is associated with a unique bond ID.

- The Fabric server creates a `BondClient` for each new plan, starts the bond, and monitors its state.

- When the plan is complete or needs to be stopped, the bond is stopped and cleaned up.

- If the bond is broken unexpectedly, the system can take recovery actions or log the failure.

## Parameters
- `capability_client.bond_topic`: The ROS parameter specifying the bond topic (default: `/capabilities/bonds`).

## Example Flow
1. **Initialization**: `BondClient` is constructed with the node and bond ID.

2. **Start**: `start()` is called to begin the bond heartbeat.

3. **Monitor**: The bond state is monitored via `on_formed()` and `on_broken()` callbacks.

4. **Stop**: `stop()` is called when the plan is done or the client is destroyed.

---

The `BondClient` ensures robust monitoring of the connection between Fabric and the capabilities server, enabling reliable plan execution and graceful handling of disconnections or failures.
