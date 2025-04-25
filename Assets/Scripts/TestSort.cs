using GPUInt64Sorting.Runtime;
using UnityEngine;
using UnityEngine.Rendering;

public class TestSort : MonoBehaviour
{
        public ComputeShader m_computeShader;
        private static int count = 100;
        
        private GPUInt64Sorting.Runtime.DeviceRadixSort m_sorter;

        // private GraphicsBuffer m_GpuSortDistances;
        // private GraphicsBuffer m_GpuSortKeys;
        
        protected const int k_partitionSize = 3840;
        protected const int k_maxTestSize = 65535 * k_partitionSize - 1;
        
        protected GraphicsBuffer m_keys;
        protected GraphicsBuffer m_payloads;
        
        protected GraphicsBuffer altKey;
        protected GraphicsBuffer altPayload;
        protected GraphicsBuffer globalHist;
        protected GraphicsBuffer passHist;
        
        private int[] m_writeArray = new int[count];
        // private Int2[] m_writeValueArray = new Int2[count];
        private int[] m_writeValueArray = new int[count];
        private int[] m_readArray = new int[count];
        // private Int2[] m_readValueArray = new Int2[count];
        private int[] m_readValueArray = new int[count];
        
        private void Start()
        {
            if (m_computeShader != null)
            {
                for (int i = 0; i < m_writeArray.Length; i++) {
                    m_writeArray[i] = (ulong)UnityEngine.Random.Range(1, 11);
                    // m_writeValueArray[i] = new Int2 { x = m_writeArray[i], y = i };
                    m_writeValueArray[i] = (ulong)i;
                }
                
                Debug.Log("Key: " + string.Join(", ", m_writeArray));
                Debug.Log("Value: " + string.Join(", ", m_writeValueArray));
                // var str1 = "Value: ";
                // for (int i = 0; i < m_writeValueArray.Length; i++)
                // {
                //     str1 += " " + i + ": " + "x: " + m_writeValueArray[i].x + ", y: " + m_writeValueArray[i].y + ";";
                // }
                // Debug.Log(str1);

                m_sorter = new DeviceRadixSort(
                    m_computeShader,
                    k_maxTestSize,
                    ref altKey,
                    ref altPayload,
                    ref globalHist,
                    ref passHist
                );
                
                m_keys = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 4) { name = "SortKey" };
                m_keys.SetData(m_writeArray);
                m_payloads = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 4 * 2) { name = "SortPayload" };
                m_payloads.SetData(m_writeValueArray);
                
                var cmd = new CommandBuffer { name = "GPUInt64Sorting" };
                m_sorter.Sort(
                    cmd,
                    count,
                    m_keys,
                    m_payloads,
                    altKey,
                    altPayload,
                    globalHist,
                    passHist,
                    typeof(int),
                    typeof(int),
                    true
                    );
                Graphics.ExecuteCommandBuffer(cmd);
                
                m_keys.GetData(m_readArray);
                Debug.Log("Key: " + string.Join(", ", m_readArray));
                m_payloads.GetData(m_readValueArray);
                Debug.Log("Value: " + string.Join(", ", m_readValueArray));
                // var str2 = "Value: ";
                // for (int i = 0; i < m_readValueArray.Length; i++)
                // {
                //     str2 += " " + i + ": " + "x: " + m_readValueArray[i].x + ", y: " + m_readValueArray[i].y + ";";
                // }
                // Debug.Log(str2);
                
                m_keys.Dispose();
                m_payloads.Dispose();
                m_keys?.Dispose();
                altPayload?.Dispose();
                globalHist?.Dispose();
                passHist?.Dispose();
            }
        }
}
