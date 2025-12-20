import { createClient } from '@connectrpc/connect'
import { transport } from './transport'
import { ClusterService } from '../gen/cluster_connect'
import { ComputeService } from '../gen/compute_connect'

export const clusterClient = createClient(ClusterService, transport)
export const computeClient = createClient(ComputeService, transport)