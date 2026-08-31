# Kubernetes Demo Application

This demo creates a real service dependency chain for the infrastructure
debugger:

```text
browser -> frontend -> checkout -> payment
```

Each component is a separate FastAPI application, container image, Kubernetes
Deployment, pod, and Service. The application is intentionally deterministic:
payment approval is simulated and no external payment platform is contacted.

## 1. Prepare the Ubuntu VM

Install Docker using the Ubuntu package, then allow the current user to run it:

```bash
sudo apt update
sudo apt install -y curl docker.io
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
```

Log out of the VM and back in so the Docker group membership takes effect.
Confirm Docker works:

```bash
docker version
```

Install a single-node k3s cluster:

```bash
curl -sfL https://get.k3s.io | sh -
sudo k3s kubectl get nodes
```

Wait until the node reports `Ready` before continuing.

## 2. Build and import the images

From the repository root on the VM:

```bash
bash scripts/build-and-import-images.sh
```

The script builds the three images with Docker, saves them to a temporary
archive, and imports them into the k3s containerd image store. No remote image
registry is required.

## 3. Deploy the application

```bash
bash scripts/deploy-demo.sh
```

There should be one running pod for each component:

```bash
sudo k3s kubectl get pods -n infrastructure-demo
```

## 4. Call the application

Find the VM's IP address:

```bash
hostname -I
```

From the VM or Mac, replace `<vm-ip>` below:

```bash
curl http://<vm-ip>:30080/api/order
```

You can also open `http://<vm-ip>:30080` in a browser. A successful response
contains nested results from `frontend`, `checkout`, and `payment`, all sharing
the same `request_id`.

If the request works inside the VM but not from the Mac, confirm that UTM's
network mode allows host-to-guest connections and that the VM firewall permits
the NodePort:

```bash
sudo ufw allow 30080/tcp
```

## Inspect and update

Show logs from all three components:

```bash
sudo k3s kubectl logs -n infrastructure-demo deployment/frontend
sudo k3s kubectl logs -n infrastructure-demo deployment/checkout
sudo k3s kubectl logs -n infrastructure-demo deployment/payment
```

After changing application code, rebuild and reimport the images, then restart
the Deployments because the local image tag remains `v1`:

```bash
bash scripts/build-and-import-images.sh
sudo k3s kubectl rollout restart deployment -n infrastructure-demo
sudo k3s kubectl rollout status deployment/frontend -n infrastructure-demo
```

If a pod reports `ErrImageNeverPull`, verify that the images were imported into
the `k8s.io` containerd namespace:

```bash
sudo k3s ctr --namespace k8s.io images list | grep infrastructure-debugger
```
