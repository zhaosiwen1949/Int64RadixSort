using System;
using GPUInt64Sorting.Runtime;
using UnityEngine;
using UnityEngine.Rendering;

public class TestSort : MonoBehaviour
{
        public ComputeShader m_computeShader;
        public ComputeShader m_initShader;
        private const int count = 16;
        
        private GPUInt64Sorting.Runtime.DeviceRadixSort m_sorter;
        
        protected const int k_partitionSize = 3840;
        protected const int k_maxTestSize = count + 10;

        protected GraphicsBuffer m_numArgs;
        protected GraphicsBuffer m_keys;
        protected GraphicsBuffer m_payloads;
        protected GraphicsBuffer m_float_payloads;
        
        protected GraphicsBuffer altKey;
        protected GraphicsBuffer altPayload;
        protected GraphicsBuffer globalHist;
        protected GraphicsBuffer passHist;
        
        private ulong[] m_writeArray = new ulong[count];
        // private Int2[] m_writeValueArray = new Int2[count];
        private uint[] m_writeValueArray = new uint[count];
        private ulong[] m_readArray = new ulong[count];
        private uint[] m_readValueArray = new uint[count];
        private float[] m_readFloatValueArray = new float[count];

        private void Start()
        {
            if (m_computeShader != null && m_initShader != null)
            {
                m_sorter = new DeviceRadixSort(
                    m_computeShader,
                    k_maxTestSize,
                    ref altKey,
                    ref altPayload,
                    ref globalHist,
                    ref passHist
                );
                
                m_numArgs = new GraphicsBuffer(GraphicsBuffer.Target.Structured, 4, 4) { name = "NumArgs" };
                m_keys = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 8) { name = "SortKey" };
                // m_keys.SetData(m_writeArray);
                m_payloads = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 4) { name = "SortPayload" };
                m_float_payloads = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 4) { name = "SortFloatPayload" };
                // m_payloads.SetData(m_writeValueArray);
            }
        }

        private void Update()
        {
            if (m_computeShader != null && m_initShader != null)
            {
                // for (int i = 0; i < m_writeArray.Length; i++) {
                //     m_writeArray[i] = (ulong)UnityEngine.Random.Range(1, 6);
                //     // m_writeValueArray[i] = new Int2 { x = m_writeArray[i], y = i };
                //     m_writeValueArray[i] = (uint)i;
                // }
                //
                // Debug.Log("Key: " + string.Join(", ", m_writeArray));
                // Debug.Log("Value: " + string.Join(", ", m_writeValueArray));
                // var str1 = "Value: ";
                // for (int i = 0; i < m_writeValueArray.Length; i++)
                // {
                //     str1 += " " + i + ": " + "x: " + m_writeValueArray[i].x + ", y: " + m_writeValueArray[i].y + ";";
                // }
                // Debug.Log(str1);

                // m_sorter = new DeviceRadixSort(
                //     m_computeShader,
                //     k_maxTestSize,
                //     ref altKey,
                //     ref altPayload,
                //     ref globalHist,
                //     ref passHist
                // );
                //
                // m_keys = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 8) { name = "SortKey" };
                // // m_keys.SetData(m_writeArray);
                // m_payloads = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 4) { name = "SortPayload" };
                // m_float_payloads = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 4) { name = "SortFloatPayload" };
                // // m_payloads.SetData(m_writeValueArray);
                
                var cmd = new CommandBuffer { name = "GPUInt64Sorting" };

                cmd.SetComputeIntParam(m_initShader, "Size", count);
                cmd.SetComputeBufferParam(m_initShader, 0, "_Keys", m_keys);
                cmd.SetComputeBufferParam(m_initShader, 0, "_Payloads", m_payloads);
                cmd.SetComputeBufferParam(m_initShader, 0, "_Float_Payloads", m_float_payloads);
                cmd.DispatchCompute(m_initShader, 0, count / 8, 1, 1);
                
                cmd.SetComputeIntParam(m_initShader, "Size", count);
                cmd.SetComputeIntParam(m_initShader, "e_min", 2);
                cmd.SetComputeIntParam(m_initShader, "e_max", k_maxTestSize);
                cmd.SetComputeIntParam(m_initShader, "e_partitionSize", k_partitionSize);
                cmd.SetComputeBufferParam(m_initShader, 1, "e_numArgs", m_numArgs);
                cmd.DispatchCompute(m_initShader, 1, 1, 1, 1);
                
                m_sorter.Sort(
                    cmd,
                    m_numArgs,
                    m_keys,
                    m_payloads,
                    altKey,
                    altPayload,
                    globalHist,
                    passHist,
                    typeof(ulong),
                    typeof(uint),
                    true
                    );

                cmd.RequestAsyncReadback(m_keys, OnReadbackKeys);
                cmd.RequestAsyncReadback(m_payloads, OnReadbackValues);
                cmd.RequestAsyncReadback(m_numArgs, OnReadbackArgs);
                cmd.WaitAllAsyncReadbackRequests();
                GraphicsFence fence = cmd.CreateGraphicsFence(GraphicsFenceType.AsyncQueueSynchronisation, SynchronisationStageFlags.AllGPUOperations);
                cmd.WaitOnAsyncGraphicsFence(fence);
                Graphics.ExecuteCommandBuffer(cmd);
                
                // m_keys.GetData(m_readArray);
                // Debug.Log("Key: " + string.Join(", ", m_readArray));
                // m_payloads.GetData(m_readValueArray);
                // Debug.Log("Value: " + string.Join(", ", m_readValueArray));
                // m_float_payloads.GetData(m_readFloatValueArray);
                // Debug.Log("Value: " + string.Join(", ", m_readFloatValueArray));
                // var str2 = "Value: ";
                // for (int i = 0; i < m_readValueArray.Length; i++)
                // {
                //     str2 += " " + i + ": " + "x: " + m_readValueArray[i].x + ", y: " + m_readValueArray[i].y + ";";
                // }
                // Debug.Log(str2);
            }
        }

        private void OnReadbackKeys(AsyncGPUReadbackRequest request)
        {
            if (request.hasError)
            {
                Debug.LogError("GPU Keys readback error");
                return;
            }

            // 获取数据
            ulong[] data = request.GetData<ulong>().ToArray();
            Debug.Log("Key: " + string.Join(", ", data));
        }
        
        private void OnReadbackValues(AsyncGPUReadbackRequest request)
        {
            if (request.hasError)
            {
                Debug.LogError("GPU Values readback error");
                return;
            }

            // 获取数据
            uint[] data = request.GetData<uint>().ToArray();
            Debug.Log("Value: " + string.Join(", ", data));
        }
        
        private void OnReadbackArgs(AsyncGPUReadbackRequest request)
        {
            if (request.hasError)
            {
                Debug.LogError("GPU Values readback error");
                return;
            }

            // 获取数据
            uint[] data = request.GetData<uint>().ToArray();
            Debug.Log("Args: " + string.Join(", ", data));
        }

        private void OnDestroy()
        {
            m_numArgs.Dispose();
            m_keys.Dispose();
            m_payloads.Dispose();
            m_float_payloads.Dispose();
            altKey?.Dispose();
            altPayload?.Dispose();
            globalHist?.Dispose();
            passHist?.Dispose();
        }
}
