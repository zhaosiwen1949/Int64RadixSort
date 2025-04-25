using System;
using UnityEngine;
using UnityEngine.Rendering;


namespace Tests
{
    struct Int2
    {
        public int x;
        public int y;
    }
    public class SortTest : MonoBehaviour
    {
        public ComputeShader m_computeShader;
        private static int count = 100;
        
        private GPUSorting.Runtime.ForwardSweep m_fs;

        // private GraphicsBuffer m_GpuSortDistances;
        // private GraphicsBuffer m_GpuSortKeys;
        
        protected const int k_partitionSize = 3840;
        protected const int k_maxTestSize = 65535 * k_partitionSize - 1;
        
        protected ComputeBuffer m_GpuSortDistances;
        protected ComputeBuffer m_GpuSortKeys;
        
        protected ComputeBuffer alt;
        protected ComputeBuffer altPayload;
        protected ComputeBuffer globalHist;
        protected ComputeBuffer passHist;
        protected ComputeBuffer index;
        
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
                    m_writeArray[i] = UnityEngine.Random.Range(1, 11);
                    // m_writeValueArray[i] = new Int2 { x = m_writeArray[i], y = i };
                    m_writeValueArray[i] = i;
                }
                
                Debug.Log("Key: " + string.Join(", ", m_writeArray));
                Debug.Log("Value: " + string.Join(", ", m_writeValueArray));
                // var str1 = "Value: ";
                // for (int i = 0; i < m_writeValueArray.Length; i++)
                // {
                //     str1 += " " + i + ": " + "x: " + m_writeValueArray[i].x + ", y: " + m_writeValueArray[i].y + ";";
                // }
                // Debug.Log(str1);
                
                m_fs = new GPUSorting.Runtime.ForwardSweep(
                    m_computeShader,
                    k_maxTestSize,
                    ref alt,
                    ref altPayload,
                    ref globalHist,
                    ref passHist,
                    ref index);
                
                // m_GpuSortDistances = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 4) { name = "GaussianSplatSortDistances" };
                // m_GpuSortDistances.SetData(m_writeArray);
                // m_GpuSortKeys = new GraphicsBuffer(GraphicsBuffer.Target.Structured, count, 4 * 2) { name = "GaussianSplatSortIndices" };
                // m_GpuSortKeys.SetData(m_writeValueArray);
                
                // m_SorterArgs.inputKeys = m_GpuSortDistances;
                // m_SorterArgs.inputValues = m_GpuSortKeys;
                // // m_SorterArgs.count = (uint)count;
                // m_SorterArgs.count = 30;
                // if (m_Sorter.Valid)
                //     m_SorterArgs.resources = GpuSorting.SupportResources.Load(m_SorterArgs.count * 2);
                // var cmd = new CommandBuffer { name = "GPUSorting" };
                // m_Sorter.Dispatch(cmd, m_SorterArgs);
                // Graphics.ExecuteCommandBuffer(cmd);
                
                m_GpuSortDistances = new ComputeBuffer(count, 4) { name = "GaussianSplatSortDistances" };
                m_GpuSortDistances.SetData(m_writeArray);
                m_GpuSortKeys = new ComputeBuffer(count, 4 * 2) { name = "GaussianSplatSortIndices" };
                m_GpuSortKeys.SetData(m_writeValueArray);
                
                m_fs.Sort(
                    count,
                    m_GpuSortDistances,
                    m_GpuSortKeys,
                    alt,
                    altPayload,
                    globalHist,
                    passHist,
                    index,
                    typeof(int),
                    typeof(int),
                    false);
                
                m_GpuSortDistances.GetData(m_readArray);
                Debug.Log("Key: " + string.Join(", ", m_readArray));
                m_GpuSortKeys.GetData(m_readValueArray);
                Debug.Log("Value: " + string.Join(", ", m_readValueArray));
                // var str2 = "Value: ";
                // for (int i = 0; i < m_readValueArray.Length; i++)
                // {
                //     str2 += " " + i + ": " + "x: " + m_readValueArray[i].x + ", y: " + m_readValueArray[i].y + ";";
                // }
                // Debug.Log(str2);
                
                m_GpuSortDistances.Dispose();
                m_GpuSortKeys.Dispose();
                alt?.Dispose();
                altPayload?.Dispose();
                globalHist?.Dispose();
                passHist?.Dispose();
                index?.Dispose();
            }
        }
    }
}