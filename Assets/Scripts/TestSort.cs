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
                
                m_sorter.Sort(
                    cmd,
                    count,
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
                Graphics.ExecuteCommandBuffer(cmd);
                
                m_keys.GetData(m_readArray);
                Debug.Log("Key: " + string.Join(", ", m_readArray));
                m_payloads.GetData(m_readValueArray);
                Debug.Log("Value: " + string.Join(", ", m_readValueArray));
                m_float_payloads.GetData(m_readFloatValueArray);
                Debug.Log("Value: " + string.Join(", ", m_readFloatValueArray));
                // var str2 = "Value: ";
                // for (int i = 0; i < m_readValueArray.Length; i++)
                // {
                //     str2 += " " + i + ": " + "x: " + m_readValueArray[i].x + ", y: " + m_readValueArray[i].y + ";";
                // }
                // Debug.Log(str2);
            }
        }

        private void OnDestroy()
        {
            m_keys.Dispose();
            m_payloads.Dispose();
            m_float_payloads.Dispose();
            altKey?.Dispose();
            altPayload?.Dispose();
            globalHist?.Dispose();
            passHist?.Dispose();
        }
}
